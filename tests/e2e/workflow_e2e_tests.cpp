#include "catch2/catch_amalgamated.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "mt/json.hpp"
#include "mt/json_parser.hpp"
#include "wf/http/workflow_http_server.hpp"
#include "wf/logic/step_output_routing_logic.hpp"
#include "wf/transport/http_transport.hpp"
#include "wf/workflow_client.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"
#include "wf/workflow_worker_pool.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using workflow::GetWorkflowExecutionRequest;
using workflow::RegisterWorkflowDefinitionRequest;
using workflow::StartWorkflowExecutionRequest;
using workflow::WorkflowClient;
using workflow::WorkflowDefinitionKey;
using workflow::WorkflowExecution;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowService;
using workflow::WorkflowStepExecution;
using workflow::WorkflowWorkerPool;
using workflow::http::WorkflowHttpServer;
using workflow::logic::StepOutputRoutingLogic;
using workflow::transport::HttpTransport;

namespace
{

const char* ORDER_WORKFLOW_JSON = R"json(
{
  "workflowName": "orderProcessing",
  "workflowVersion": 1,
  "startWorkflowStepName": "validateOrder",
  "expectedExecutionTime": "PT10M",
  "steps": [
    {"name": "validateOrder",  "expectedExecutionTime": "PT30S", "maxRetries": 1},
    {"name": "chargePayment",  "expectedExecutionTime": "PT2M",  "maxRetries": 0}
  ]
}
)json";

const char* FULFILL_WORKFLOW_JSON = R"json(
{
  "workflowName": "fulfillOrder",
  "workflowVersion": 1,
  "startWorkflowStepName": "shipOrder",
  "expectedExecutionTime": "PT5M",
  "steps": [
    {"name": "shipOrder", "expectedExecutionTime": "PT1M", "maxRetries": 0}
  ]
}
)json";

// Two separate WorkflowClients share the same server:
//   poolClient    — owned by the WorkflowWorkerPool; all calls serialized by clientMutex
//   observerClient — used only from the test thread for assertions; no concurrent access
struct E2ETestContext
{
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
    StepOutputRoutingLogic logic;
    WorkflowOrchestrator orchestrator;
    WorkflowService service;
    WorkflowHttpServer server;
    int port;
    std::thread serverThread;

    WorkflowClient poolClient;
    WorkflowClient observerClient;

    E2ETestContext()
        : orchestrator(
              database,
              logic
          ),
          service(orchestrator),
          server(
              service,
              0
          ),
          port(server.bind()),
          serverThread([this] { server.start(); }),
          poolClient(
              std::make_unique<HttpTransport>(
                  "localhost",
                  port
              )
          ),
          observerClient(
              std::make_unique<HttpTransport>(
                  "localhost",
                  port
              )
          )
    {
    }

    ~E2ETestContext()
    {
        server.stop();
        serverThread.join();
    }

    void registerWorkflow(const char* json)
    {
        observerClient.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{.definitionJson = mt::JsonParser(json).parse()}
        );
    }

    std::string startOrder()
    {
        return observerClient
            .startWorkflowExecution(
                StartWorkflowExecutionRequest{
                    .workflowName = "orderProcessing", .workflowVersion = 1
                }
            )
            .execution.workflowExecutionId;
    }

    std::string startFulfill()
    {
        return observerClient
            .startWorkflowExecution(
                StartWorkflowExecutionRequest{.workflowName = "fulfillOrder", .workflowVersion = 1}
            )
            .execution.workflowExecutionId;
    }

    WorkflowExecution waitForCompletion(const std::string& id)
    {
        for (int i = 0; i < 500; ++i)
        {
            auto res = observerClient.getWorkflowExecution(
                GetWorkflowExecutionRequest{.workflowExecutionId = id}
            );
            if (res.execution.has_value() &&
                res.execution->status != WorkflowExecutionStatus::Running)
            {
                return *res.execution;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("execution did not finish within timeout");
    }

    std::vector<WorkflowDefinitionKey> allDefinitions() const
    {
        return {
            WorkflowDefinitionKey{.workflowName = "orderProcessing", .workflowVersion = 1},
            WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1},
        };
    }
};

WorkflowWorkerPool::Options fastOptions(
    std::size_t threads = 4,
    std::size_t pollers = 2
)
{
    WorkflowWorkerPool::Options opts;
    opts.threadCount = threads;
    opts.pollerCount = pollers;
    opts.maxResultsPerPoll = 4;
    opts.pollInterval = std::chrono::milliseconds(10);
    opts.keepAliveInterval = std::chrono::hours(1);
    return opts;
}

mt::Json nextStep(const std::string& name)
{
    mt::Json::Object out;
    out["nextStep"] = name;
    return mt::Json(std::move(out));
}

mt::Json completeOutput()
{
    return mt::Json(mt::Json::Object{});
}

} // namespace

// -----------------------------------------------------------------------------
// Basic execution
// -----------------------------------------------------------------------------

TEST_CASE("e2e: single-step workflow completes over HTTP")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto id = ctx.startFulfill();

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}}, "pool-001",
        fastOptions()
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    const auto exec = ctx.waitForCompletion(id);
    pool.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("e2e: multi-step workflow routes via nextStep over HTTP")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(ORDER_WORKFLOW_JSON);
    const auto id = ctx.startOrder();

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "orderProcessing", .workflowVersion = 1}},
        "pool-001", fastOptions()
    );
    pool.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return nextStep("chargePayment"); }
    );
    pool.registerStep(
        "chargePayment", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    pool.start();

    const auto exec = ctx.waitForCompletion(id);
    pool.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("e2e: handler throws causes workflow to fail over HTTP")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto id = ctx.startFulfill();

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}}, "pool-001",
        fastOptions()
    );
    pool.registerStep(
        "shipOrder",
        [](const WorkflowStepExecution&) -> mt::Json { throw std::runtime_error("out of stock"); }
    );
    pool.start();

    const auto exec = ctx.waitForCompletion(id);
    pool.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Failed);
    REQUIRE(exec.failureReason == "out of stock");
}

TEST_CASE("e2e: no handler registered causes workflow to fail over HTTP")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto id = ctx.startFulfill();

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}}, "pool-001",
        fastOptions()
    );
    // No handler registered.
    pool.start();

    const auto exec = ctx.waitForCompletion(id);
    pool.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Failed);
}

// -----------------------------------------------------------------------------
// Multiple definitions
// -----------------------------------------------------------------------------

TEST_CASE("e2e: pool serves two workflow definitions over a single HTTP connection")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(ORDER_WORKFLOW_JSON);
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto orderId = ctx.startOrder();
    const auto fulfillId = ctx.startFulfill();

    WorkflowWorkerPool pool(ctx.poolClient, ctx.allDefinitions(), "pool-001", fastOptions());
    pool.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    const auto orderExec = ctx.waitForCompletion(orderId);
    const auto fulfillExec = ctx.waitForCompletion(fulfillId);
    pool.stop();

    REQUIRE(orderExec.status == WorkflowExecutionStatus::Completed);
    REQUIRE(fulfillExec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("e2e: one definition failing does not prevent the other from completing")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(ORDER_WORKFLOW_JSON);
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto orderId = ctx.startOrder();
    const auto fulfillId = ctx.startFulfill();

    WorkflowWorkerPool pool(ctx.poolClient, ctx.allDefinitions(), "pool-001", fastOptions());
    pool.registerStep(
        "validateOrder",
        [](const WorkflowStepExecution&) -> mt::Json { throw std::runtime_error("payment error"); }
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    const auto orderExec = ctx.waitForCompletion(orderId);
    const auto fulfillExec = ctx.waitForCompletion(fulfillId);
    pool.stop();

    REQUIRE(orderExec.status == WorkflowExecutionStatus::Failed);
    REQUIRE(fulfillExec.status == WorkflowExecutionStatus::Completed);
}

// -----------------------------------------------------------------------------
// Concurrency
// -----------------------------------------------------------------------------

TEST_CASE("e2e: multiple executions complete concurrently over HTTP")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);

    std::vector<std::string> ids;
    for (int i = 0; i < 8; ++i)
    {
        ids.push_back(ctx.startFulfill());
    }

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}}, "pool-001",
        fastOptions(4, 2)
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    for (const auto& id : ids)
    {
        REQUIRE(ctx.waitForCompletion(id).status == WorkflowExecutionStatus::Completed);
    }

    pool.stop();
}

TEST_CASE("e2e: concurrent steps never exceed threadCount")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);

    std::vector<std::string> ids;
    for (int i = 0; i < 8; ++i)
    {
        ids.push_back(ctx.startFulfill());
    }

    const std::size_t threadCount = 3;
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}}, "pool-001",
        fastOptions(threadCount, 2)
    );
    pool.registerStep(
        "shipOrder",
        [&](const WorkflowStepExecution&) -> mt::Json
        {
            const int cur = ++active;
            int prev = maxActive.load();
            while (cur > prev && !maxActive.compare_exchange_weak(prev, cur))
            {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            --active;
            return completeOutput();
        }
    );
    pool.start();

    for (const auto& id : ids)
    {
        ctx.waitForCompletion(id);
    }
    pool.stop();

    REQUIRE(maxActive.load() <= static_cast<int>(threadCount));
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

TEST_CASE("e2e: pool stop drains in-flight steps before returning")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto id = ctx.startFulfill();

    std::atomic<bool> handlerStarted{false};
    std::atomic<bool> handlerAllowFinish{false};

    WorkflowWorkerPool pool(
        ctx.poolClient,
        {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}}, "pool-001",
        fastOptions(2, 1)
    );
    pool.registerStep(
        "shipOrder",
        [&](const WorkflowStepExecution&) -> mt::Json
        {
            handlerStarted = true;
            while (!handlerAllowFinish)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return completeOutput();
        }
    );
    pool.start();

    // Wait until the handler is running, then release it and stop concurrently.
    while (!handlerStarted)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    handlerAllowFinish = true;
    pool.stop();

    // After stop() returns the step must be complete on the server.
    const auto exec = ctx.observerClient.getWorkflowExecution(
        GetWorkflowExecutionRequest{.workflowExecutionId = id}
    );
    REQUIRE(exec.execution.has_value());
    REQUIRE(exec.execution->status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("e2e: pool destructor stops the loop and completes in-flight work")
{
    E2ETestContext ctx;
    ctx.registerWorkflow(FULFILL_WORKFLOW_JSON);
    const auto id = ctx.startFulfill();

    {
        WorkflowWorkerPool pool(
            ctx.poolClient,
            {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}},
            "pool-001", fastOptions()
        );
        pool.registerStep(
            "shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
        );
        pool.start();
        ctx.waitForCompletion(id);
        // destructor calls stop()
    }

    const auto exec = ctx.observerClient.getWorkflowExecution(
        GetWorkflowExecutionRequest{.workflowExecutionId = id}
    );
    REQUIRE(exec.execution->status == WorkflowExecutionStatus::Completed);
}
