#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"
#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"
#include "wf/json.hpp"
#include "wf/logic/step_output_routing_logic.hpp"
#include "wf/transport/in_process_transport.hpp"
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
using workflow::backend::memory::InMemoryWorkflowDefinitionStore;
using workflow::backend::memory::InMemoryWorkflowExecutionStore;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;
using workflow::json::Value;
using workflow::logic::StepOutputRoutingLogic;
using workflow::transport::InProcessTransport;

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

WorkflowWorkerPool::Options fastPoolOptions(
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

struct PoolTestContext
{
    InMemoryWorkflowDefinitionStore definitionStore;
    InMemoryWorkflowExecutionStore executionStore;
    InMemoryWorkflowStepExecutionStore stepExecutionStore;
    StepOutputRoutingLogic logic;
    WorkflowOrchestrator orchestrator;
    WorkflowService service;
    WorkflowClient client;

    PoolTestContext()
        : orchestrator(
              definitionStore,
              executionStore,
              stepExecutionStore,
              logic
          ),
          service(orchestrator),
          client(std::make_unique<InProcessTransport>(service))
    {
        client.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{
                .definitionJson = workflow::json::parse(ORDER_WORKFLOW_JSON)
            }
        );
        client.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{
                .definitionJson = workflow::json::parse(FULFILL_WORKFLOW_JSON)
            }
        );
    }

    std::string startOrder()
    {
        return client
            .startWorkflowExecution(
                StartWorkflowExecutionRequest{
                    .workflowName = "orderProcessing", .workflowVersion = 1
                }
            )
            .execution.workflowExecutionId;
    }

    std::string startFulfill()
    {
        return client
            .startWorkflowExecution(
                StartWorkflowExecutionRequest{.workflowName = "fulfillOrder", .workflowVersion = 1}
            )
            .execution.workflowExecutionId;
    }

    WorkflowExecution waitForCompletion(const std::string& id)
    {
        for (int i = 0; i < 500; ++i)
        {
            auto res =
                client.getWorkflowExecution(GetWorkflowExecutionRequest{.workflowExecutionId = id});
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

Value nextStep(const std::string& name)
{
    Value::Object out;
    out["nextStep"] = name;
    return Value(std::move(out));
}

Value completeOutput()
{
    return Value::object();
}

} // namespace

TEST_CASE("WorkflowWorkerPool completes a single-step workflow")
{
    PoolTestContext ctx;
    const auto id = ctx.startFulfill();

    WorkflowWorkerPool pool(
        ctx.client, {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}},
        "pool-001", fastPoolOptions()
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    const auto exec = ctx.waitForCompletion(id);
    pool.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("WorkflowWorkerPool routes through multiple steps via nextStep")
{
    PoolTestContext ctx;
    const auto id = ctx.startOrder();

    WorkflowWorkerPool pool(
        ctx.client,
        {WorkflowDefinitionKey{.workflowName = "orderProcessing", .workflowVersion = 1}},
        "pool-001", fastPoolOptions()
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

TEST_CASE("WorkflowWorkerPool serves multiple workflow definitions concurrently")
{
    PoolTestContext ctx;
    const auto orderId = ctx.startOrder();
    const auto fulfillId = ctx.startFulfill();

    WorkflowWorkerPool pool(ctx.client, ctx.allDefinitions(), "pool-001", fastPoolOptions());
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

TEST_CASE("WorkflowWorkerPool executes steps concurrently across multiple executions")
{
    PoolTestContext ctx;

    std::vector<std::string> ids;
    for (int i = 0; i < 8; ++i)
    {
        ids.push_back(ctx.startFulfill());
    }

    WorkflowWorkerPool pool(
        ctx.client, {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}},
        "pool-001", fastPoolOptions(4, 2)
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    for (const auto& id : ids)
    {
        const auto exec = ctx.waitForCompletion(id);
        REQUIRE(exec.status == WorkflowExecutionStatus::Completed);
    }
    pool.stop();
}

TEST_CASE("WorkflowWorkerPool tracks concurrency: active steps never exceed threadCount")
{
    PoolTestContext ctx;

    std::vector<std::string> ids;
    for (int i = 0; i < 8; ++i)
    {
        ids.push_back(ctx.startFulfill());
    }

    const std::size_t threadCount = 3;
    std::atomic<int> activeSteps{0};
    std::atomic<int> maxObservedActive{0};

    WorkflowWorkerPool pool(
        ctx.client, {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}},
        "pool-001", fastPoolOptions(threadCount, 2)
    );
    pool.registerStep(
        "shipOrder",
        [&](const WorkflowStepExecution&) -> Value
        {
            const int current = ++activeSteps;
            int expected = maxObservedActive.load();
            while (current > expected &&
                   !maxObservedActive.compare_exchange_weak(expected, current))
            {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            --activeSteps;
            return completeOutput();
        }
    );
    pool.start();

    for (const auto& id : ids)
    {
        ctx.waitForCompletion(id);
    }
    pool.stop();

    REQUIRE(maxObservedActive.load() <= static_cast<int>(threadCount));
}

TEST_CASE("WorkflowWorkerPool fails step when handler throws; pool keeps running")
{
    PoolTestContext ctx;
    const auto failId = ctx.startFulfill();
    const auto okId = ctx.startOrder();

    WorkflowWorkerPool pool(ctx.client, ctx.allDefinitions(), "pool-001", fastPoolOptions());
    pool.registerStep(
        "shipOrder",
        [](const WorkflowStepExecution&) -> Value { throw std::runtime_error("ship failed"); }
    );
    pool.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    pool.start();

    const auto failExec = ctx.waitForCompletion(failId);
    const auto okExec = ctx.waitForCompletion(okId);
    pool.stop();

    REQUIRE(failExec.status == WorkflowExecutionStatus::Failed);
    REQUIRE(failExec.failureReason == "ship failed");
    REQUIRE(okExec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("WorkflowWorkerPool fails step immediately when no handler registered")
{
    PoolTestContext ctx;
    const auto id = ctx.startFulfill();

    WorkflowWorkerPool pool(
        ctx.client, {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}},
        "pool-001", fastPoolOptions()
    );
    // No handler for "shipOrder" registered.
    pool.start();

    const auto exec = ctx.waitForCompletion(id);
    pool.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Failed);
}

TEST_CASE("WorkflowWorkerPool stop is safe to call before start")
{
    PoolTestContext ctx;
    WorkflowWorkerPool pool(ctx.client, ctx.allDefinitions(), "pool-001", fastPoolOptions());
    pool.stop();
}

TEST_CASE("WorkflowWorkerPool destructor stops the pool")
{
    PoolTestContext ctx;
    const auto id = ctx.startFulfill();

    {
        WorkflowWorkerPool pool(
            ctx.client,
            {WorkflowDefinitionKey{.workflowName = "fulfillOrder", .workflowVersion = 1}},
            "pool-001", fastPoolOptions()
        );
        pool.registerStep(
            "shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
        );
        pool.start();
        ctx.waitForCompletion(id);
        // destructor calls stop()
    }

    const auto exec =
        ctx.client.getWorkflowExecution(GetWorkflowExecutionRequest{.workflowExecutionId = id});
    REQUIRE(exec.execution->status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("WorkflowWorkerPool serves minority definition despite heavy backlog on another")
{
    // Regression for head-of-list bias: if one definition monopolises executor
    // slots the other must still complete within the timeout.
    PoolTestContext ctx;

    // Start many executions for orderProcessing (the "heavy" definition).
    std::vector<std::string> heavyIds;
    for (int i = 0; i < 8; ++i)
    {
        heavyIds.push_back(ctx.startOrder());
    }

    // Start one execution for fulfillOrder (the "minority" definition).
    const auto minorityId = ctx.startFulfill();

    WorkflowWorkerPool pool(ctx.client, ctx.allDefinitions(), "pool-001", fastPoolOptions(4, 2));
    pool.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    pool.registerStep("shipOrder", [](const WorkflowStepExecution&) { return completeOutput(); });
    pool.start();

    // The minority definition must complete even though the heavy one produces
    // far more work.
    const auto minorityExec = ctx.waitForCompletion(minorityId);
    REQUIRE(minorityExec.status == WorkflowExecutionStatus::Completed);

    for (const auto& id : heavyIds)
    {
        REQUIRE(ctx.waitForCompletion(id).status == WorkflowExecutionStatus::Completed);
    }
    pool.stop();
}

TEST_CASE("WorkflowWorkerPool with single poller handles multiple definitions")
{
    PoolTestContext ctx;
    const auto orderId = ctx.startOrder();
    const auto fulfillId = ctx.startFulfill();

    WorkflowWorkerPool pool(ctx.client, ctx.allDefinitions(), "pool-001", fastPoolOptions(4, 1));
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
