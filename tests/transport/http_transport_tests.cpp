#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"
#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"
#include "wf/http/workflow_http_server.hpp"
#include "wf/json.hpp"
#include "wf/transport/http_transport.hpp"
#include "wf/workflow_client.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"

#include <memory>
#include <string>
#include <thread>
#include <vector>

using workflow::CancelWorkflowRequest;
using workflow::CompleteWorkflowStepRequest;
using workflow::FailWorkflowStepRequest;
using workflow::GetWorkflowExecutionRequest;
using workflow::KeepAliveWorkflowStepRequest;
using workflow::ListWorkflowDefinitionsRequest;
using workflow::NextStepDecision;
using workflow::PollAndClaimWorkflowStepsRequest;
using workflow::RegisterWorkflowDefinitionRequest;
using workflow::StartWorkflowExecutionRequest;
using workflow::StepCompletionContext;
using workflow::StepExecutionStatus;
using workflow::ValidateWorkflowDefinitionRequest;
using workflow::WorkflowClient;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowLogic;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowService;
using workflow::backend::memory::InMemoryWorkflowDefinitionStore;
using workflow::backend::memory::InMemoryWorkflowExecutionStore;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;
using workflow::http::WorkflowHttpServer;
using workflow::transport::HttpTransport;

namespace
{

const char* VALID_WORKFLOW_JSON = R"json(
{
  "workflowName": "orderProcessing",
  "workflowVersion": 1,
  "startWorkflowStepName": "validateOrder",
  "expectedExecutionTime": "PT10M",
  "steps": [
    {
      "name": "validateOrder",
      "expectedExecutionTime": "PT30S",
      "maxRetries": 2
    },
    {
      "name": "chargePayment",
      "expectedExecutionTime": "PT2M",
      "maxRetries": 1
    }
  ]
}
)json";

NextStepDecision completeWorkflowDecision()
{
    NextStepDecision decision;
    decision.workflowComplete = true;
    decision.updatedState = workflow::json::Value::object();
    return decision;
}

NextStepDecision nextStepDecision(const std::string& stepName)
{
    NextStepDecision decision;
    decision.workflowComplete = false;
    decision.nextStepName = stepName;
    decision.updatedState = workflow::json::Value::object();
    return decision;
}

class ScriptedWorkflowLogic final : public WorkflowLogic
{
  public:
    explicit ScriptedWorkflowLogic(std::vector<NextStepDecision> decisions = {})
        : decisions_(std::move(decisions))
    {
    }

    NextStepDecision decideNextStep(const StepCompletionContext&) override
    {
        if (nextIndex_ >= decisions_.size())
        {
            return completeWorkflowDecision();
        }
        return decisions_.at(nextIndex_++);
    }

  private:
    std::vector<NextStepDecision> decisions_;
    std::size_t nextIndex_ = 0;
};

struct HttpTransportTestContext
{
    InMemoryWorkflowDefinitionStore definitionStore;
    InMemoryWorkflowExecutionStore executionStore;
    InMemoryWorkflowStepExecutionStore stepExecutionStore;
    ScriptedWorkflowLogic logic;
    WorkflowOrchestrator orchestrator;
    WorkflowService service;
    WorkflowHttpServer server;
    int port;
    std::thread serverThread;
    WorkflowClient client;

    explicit HttpTransportTestContext(std::vector<NextStepDecision> decisions = {})
        : logic(std::move(decisions)),
          orchestrator(
              definitionStore,
              executionStore,
              stepExecutionStore,
              logic
          ),
          service(orchestrator),
          server(
              service,
              0
          ),
          port(server.bind()),
          serverThread([this] { server.start(); }),
          client(
              std::make_unique<HttpTransport>(
                  "localhost",
                  port
              )
          )
    {
    }

    ~HttpTransportTestContext()
    {
        server.stop();
        serverThread.join();
    }

    void registerWorkflow()
    {
        client.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{
                .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON)
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
};

} // namespace

TEST_CASE("HttpTransport validate workflow definition")
{
    HttpTransportTestContext ctx;

    SECTION("valid definition returns valid")
    {
        auto res = ctx.client.validateWorkflowDefinition(
            ValidateWorkflowDefinitionRequest{
                .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON)
            }
        );
        REQUIRE(res.validation.valid);
        REQUIRE(res.validation.errors.empty());
    }

    SECTION("invalid definition returns errors")
    {
        auto res = ctx.client.validateWorkflowDefinition(
            ValidateWorkflowDefinitionRequest{.definitionJson = workflow::json::Value::object()}
        );
        REQUIRE_FALSE(res.validation.valid);
        REQUIRE_FALSE(res.validation.errors.empty());
    }
}

TEST_CASE("HttpTransport register and list workflow definitions")
{
    HttpTransportTestContext ctx;

    SECTION("list returns empty before registration")
    {
        auto res = ctx.client.listWorkflowDefinitions(ListWorkflowDefinitionsRequest{});
        REQUIRE(res.definitions.empty());
    }

    SECTION("register then list returns definition")
    {
        ctx.registerWorkflow();

        auto res = ctx.client.listWorkflowDefinitions(ListWorkflowDefinitionsRequest{});
        REQUIRE(res.definitions.size() == 1);
        REQUIRE(res.definitions[0].workflowName == "orderProcessing");
        REQUIRE(res.definitions[0].workflowVersion == 1);
    }
}

TEST_CASE("HttpTransport start workflow execution")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();

    SECTION("start returns running execution")
    {
        auto res = ctx.client.startWorkflowExecution(
            StartWorkflowExecutionRequest{.workflowName = "orderProcessing", .workflowVersion = 1}
        );
        REQUIRE(res.execution.status == WorkflowExecutionStatus::Running);
        REQUIRE(res.execution.workflowName == "orderProcessing");
        REQUIRE(res.execution.workflowVersion == 1);
        REQUIRE(res.execution.currentStepName == "validateOrder");
        REQUIRE_FALSE(res.execution.workflowExecutionId.empty());
    }

    SECTION("start with unknown workflow throws")
    {
        REQUIRE_THROWS(ctx.client.startWorkflowExecution(
            StartWorkflowExecutionRequest{.workflowName = "unknown", .workflowVersion = 1}
        ));
    }
}

TEST_CASE("HttpTransport get workflow execution")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();
    const auto id = ctx.startExecution();

    SECTION("get existing execution")
    {
        auto res =
            ctx.client.getWorkflowExecution(GetWorkflowExecutionRequest{.workflowExecutionId = id});
        REQUIRE(res.execution.has_value());
        REQUIRE(res.execution->workflowExecutionId == id);
        REQUIRE(res.execution->status == WorkflowExecutionStatus::Running);
    }

    SECTION("get unknown execution returns empty")
    {
        auto res = ctx.client.getWorkflowExecution(
            GetWorkflowExecutionRequest{.workflowExecutionId = "no-such-id"}
        );
        REQUIRE_FALSE(res.execution.has_value());
    }
}

TEST_CASE("HttpTransport poll and claim workflow steps")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();
    ctx.startExecution();

    SECTION("poll returns pending step")
    {
        auto res = ctx.client.pollAndClaimWorkflowSteps(
            PollAndClaimWorkflowStepsRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
                .workerId = "worker-001",
                .maxResults = 1
            }
        );
        REQUIRE(res.steps.size() == 1);
        REQUIRE(res.steps[0].stepName == "validateOrder");
        REQUIRE(res.steps[0].status == StepExecutionStatus::Running);
        REQUIRE(res.steps[0].workerId == "worker-001");
    }

    SECTION("poll with no pending steps returns empty")
    {
        ctx.client.pollAndClaimWorkflowSteps(
            PollAndClaimWorkflowStepsRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
                .workerId = "worker-001",
                .maxResults = 1
            }
        );

        auto res = ctx.client.pollAndClaimWorkflowSteps(
            PollAndClaimWorkflowStepsRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
                .workerId = "worker-002",
                .maxResults = 1
            }
        );
        REQUIRE(res.steps.empty());
    }
}

TEST_CASE("HttpTransport keep alive workflow step")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();
    const auto id = ctx.startExecution();

    ctx.client.pollAndClaimWorkflowSteps(
        PollAndClaimWorkflowStepsRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .workerId = "worker-001",
            .maxResults = 1
        }
    );

    SECTION("keep alive returns running step")
    {
        auto res = ctx.client.keepAliveWorkflowStep(
            KeepAliveWorkflowStepRequest{
                .workflowExecutionId = id, .stepName = "validateOrder", .workerId = "worker-001"
            }
        );
        REQUIRE(res.step.status == StepExecutionStatus::Running);
        REQUIRE(res.step.workerId == "worker-001");
    }

    SECTION("keep alive with wrong worker throws")
    {
        REQUIRE_THROWS(ctx.client.keepAliveWorkflowStep(
            KeepAliveWorkflowStepRequest{
                .workflowExecutionId = id, .stepName = "validateOrder", .workerId = "wrong-worker"
            }
        ));
    }
}

TEST_CASE("HttpTransport complete workflow step")
{
    HttpTransportTestContext ctx({nextStepDecision("chargePayment")});
    ctx.registerWorkflow();
    const auto id = ctx.startExecution();

    ctx.client.pollAndClaimWorkflowSteps(
        PollAndClaimWorkflowStepsRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .workerId = "worker-001",
            .maxResults = 1
        }
    );

    SECTION("complete transitions to next step")
    {
        auto res = ctx.client.completeWorkflowStep(
            CompleteWorkflowStepRequest{
                .workflowExecutionId = id, .stepName = "validateOrder", .workerId = "worker-001"
            }
        );
        REQUIRE(res.execution.status == WorkflowExecutionStatus::Running);
        REQUIRE(res.execution.currentStepName == "chargePayment");
    }

    SECTION("complete with wrong worker throws")
    {
        REQUIRE_THROWS(ctx.client.completeWorkflowStep(
            CompleteWorkflowStepRequest{
                .workflowExecutionId = id, .stepName = "validateOrder", .workerId = "wrong-worker"
            }
        ));
    }
}

TEST_CASE("HttpTransport complete last step finishes workflow")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();
    const auto id = ctx.startExecution();

    ctx.client.pollAndClaimWorkflowSteps(
        PollAndClaimWorkflowStepsRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .workerId = "worker-001",
            .maxResults = 1
        }
    );

    auto res = ctx.client.completeWorkflowStep(
        CompleteWorkflowStepRequest{
            .workflowExecutionId = id, .stepName = "validateOrder", .workerId = "worker-001"
        }
    );
    REQUIRE(res.execution.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("HttpTransport fail workflow step")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();
    const auto id = ctx.startExecution();

    ctx.client.pollAndClaimWorkflowSteps(
        PollAndClaimWorkflowStepsRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .workerId = "worker-001",
            .maxResults = 1
        }
    );

    SECTION("fail with retries remaining retries step")
    {
        auto res = ctx.client.failWorkflowStep(
            FailWorkflowStepRequest{
                .workflowExecutionId = id,
                .stepName = "validateOrder",
                .workerId = "worker-001",
                .reason = "timeout"
            }
        );
        REQUIRE(res.execution.status == WorkflowExecutionStatus::Running);
        REQUIRE(res.execution.currentStepName == "validateOrder");
    }

    SECTION("fail with wrong worker throws")
    {
        REQUIRE_THROWS(ctx.client.failWorkflowStep(
            FailWorkflowStepRequest{
                .workflowExecutionId = id,
                .stepName = "validateOrder",
                .workerId = "wrong-worker",
                .reason = "timeout"
            }
        ));
    }
}

TEST_CASE("HttpTransport cancel workflow")
{
    HttpTransportTestContext ctx;
    ctx.registerWorkflow();
    const auto id = ctx.startExecution();

    SECTION("cancel running workflow")
    {
        auto res = ctx.client.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = id});
        REQUIRE(res.execution.status == WorkflowExecutionStatus::Canceled);
    }

    SECTION("cancel unknown workflow throws")
    {
        REQUIRE_THROWS(
            ctx.client.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = "no-such-id"})
        );
    }
}
