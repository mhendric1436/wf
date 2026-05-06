#include "catch2/catch_amalgamated.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "mt/json.hpp"
#include "mt/json_parser.hpp"
#include "wf/logic/step_output_routing_logic.hpp"
#include "wf/transport/in_process_transport.hpp"
#include "wf/workflow_client.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"
#include "wf/workflow_worker.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using workflow::CompleteWorkflowStepRequest;
using workflow::GetWorkflowExecutionRequest;
using workflow::RegisterWorkflowDefinitionRequest;
using workflow::StartWorkflowExecutionRequest;
using workflow::WorkflowClient;
using workflow::WorkflowExecution;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowService;
using workflow::WorkflowStepExecution;
using workflow::WorkflowWorker;
using workflow::logic::StepOutputRoutingLogic;
using workflow::transport::InProcessTransport;

namespace
{

const char* WORKFLOW_JSON = R"json(
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

WorkflowWorker::Options fastOptions()
{
    WorkflowWorker::Options opts;
    opts.pollInterval = std::chrono::milliseconds(10);
    opts.keepAliveInterval = std::chrono::hours(1);
    return opts;
}

struct WorkerTestContext
{
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
    StepOutputRoutingLogic logic;
    WorkflowOrchestrator orchestrator;
    WorkflowService service;
    WorkflowClient client;

    WorkerTestContext()
        : orchestrator(
              database,
              logic
          ),
          service(orchestrator),
          client(std::make_unique<InProcessTransport>(service))
    {
        client.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{
                .definitionJson = mt::JsonParser(WORKFLOW_JSON).parse()
            }
        );
    }

    std::string startExecution()
    {
        auto res = client.startWorkflowExecution(
            StartWorkflowExecutionRequest{.workflowName = "orderProcessing", .workflowVersion = 1}
        );
        return res.execution.workflowExecutionId;
    }

    // Polls until execution leaves Running state or timeout.
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
};

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

TEST_CASE("WorkflowWorker completes a single-step workflow")
{
    WorkerTestContext ctx;
    const auto id = ctx.startExecution();

    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    worker.start();

    const auto exec = ctx.waitForCompletion(id);
    worker.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("WorkflowWorker routes through multiple steps via nextStep")
{
    WorkerTestContext ctx;
    const auto id = ctx.startExecution();

    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return nextStep("chargePayment"); }
    );
    worker.registerStep(
        "chargePayment", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    worker.start();

    const auto exec = ctx.waitForCompletion(id);
    worker.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("WorkflowWorker passes step execution to handler")
{
    WorkerTestContext ctx;
    const auto id = ctx.startExecution();

    std::string capturedStepName;
    std::string capturedExecutionId;

    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.registerStep(
        "validateOrder",
        [&](const WorkflowStepExecution& step)
        {
            capturedStepName = step.stepName;
            capturedExecutionId = step.workflowExecutionId;
            return completeOutput();
        }
    );
    worker.start();

    ctx.waitForCompletion(id);
    worker.stop();

    REQUIRE(capturedStepName == "validateOrder");
    REQUIRE(capturedExecutionId == id);
}

TEST_CASE("WorkflowWorker fails step when handler throws")
{
    WorkerTestContext ctx;
    const auto id = ctx.startExecution();

    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) -> mt::Json
        { throw std::runtime_error("something went wrong"); }
    );
    worker.start();

    const auto exec = ctx.waitForCompletion(id);
    worker.stop();

    // validateOrder has maxRetries=1, so one failure retries; second failure fails the workflow
    // The handler always throws, so after exhausting retries the workflow fails.
    REQUIRE(exec.status == WorkflowExecutionStatus::Failed);
    REQUIRE(exec.failureReason == "something went wrong");
}

TEST_CASE("WorkflowWorker fails step immediately when no handler registered")
{
    WorkerTestContext ctx;
    const auto id = ctx.startExecution();

    // No handler for validateOrder registered.
    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.start();

    const auto exec = ctx.waitForCompletion(id);
    worker.stop();

    REQUIRE(exec.status == WorkflowExecutionStatus::Failed);
}

TEST_CASE("WorkflowWorker processes executions from multiple workflows sequentially")
{
    WorkerTestContext ctx;
    const auto id1 = ctx.startExecution();
    const auto id2 = ctx.startExecution();

    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.registerStep(
        "validateOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
    );
    worker.start();

    const auto exec1 = ctx.waitForCompletion(id1);
    const auto exec2 = ctx.waitForCompletion(id2);
    worker.stop();

    REQUIRE(exec1.status == WorkflowExecutionStatus::Completed);
    REQUIRE(exec2.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("WorkflowWorker stop is safe to call before start")
{
    WorkerTestContext ctx;
    WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
    worker.stop(); // should not throw or hang
}

TEST_CASE("WorkflowWorker destructor stops the loop")
{
    WorkerTestContext ctx;
    const auto id = ctx.startExecution();

    {
        WorkflowWorker worker(ctx.client, "orderProcessing", 1, "worker-001", fastOptions());
        worker.registerStep(
            "validateOrder", [](const WorkflowStepExecution&) { return completeOutput(); }
        );
        worker.start();
        ctx.waitForCompletion(id);
        // destructor calls stop()
    }

    const auto exec =
        ctx.client.getWorkflowExecution(GetWorkflowExecutionRequest{.workflowExecutionId = id});
    REQUIRE(exec.execution->status == WorkflowExecutionStatus::Completed);
}
