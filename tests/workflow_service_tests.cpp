#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"
#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"
#include "wf/json.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"

#include <optional>
#include <stdexcept>
#include <string>
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
using workflow::SweepExpiredLeasesRequest;
using workflow::ValidateWorkflowDefinitionRequest;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowLogic;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowService;
using workflow::backend::memory::InMemoryWorkflowDefinitionStore;
using workflow::backend::memory::InMemoryWorkflowExecutionStore;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;

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
        if (nextDecisionIndex_ >= decisions_.size())
        {
            return completeWorkflowDecision();
        }

        return decisions_.at(nextDecisionIndex_++);
    }

  private:
    std::vector<NextStepDecision> decisions_;
    std::size_t nextDecisionIndex_ = 0;
};

struct TestContext
{
    InMemoryWorkflowDefinitionStore definitionStore;
    InMemoryWorkflowExecutionStore executionStore;
    InMemoryWorkflowStepExecutionStore stepExecutionStore;
    ScriptedWorkflowLogic logic;
    WorkflowOrchestrator orchestrator;
    WorkflowService service;

    explicit TestContext(std::vector<NextStepDecision> decisions = {})
        : logic(std::move(decisions)),
          orchestrator(
              definitionStore,
              executionStore,
              stepExecutionStore,
              logic
          ),
          service(orchestrator)
    {
    }

    void registerValidDefinition()
    {
        service.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{
                .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON),
            }
        );
    }

    std::string startExecution()
    {
        const auto response = service.startWorkflowExecution(
            StartWorkflowExecutionRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
                .input = workflow::json::Value::object(),
            }
        );
        return response.execution.workflowExecutionId;
    }

    std::string claimInitialStep(const std::string& workerId = "worker-001")
    {
        const auto executionId = startExecution();

        const auto pollResponse = service.pollAndClaimWorkflowSteps(
            PollAndClaimWorkflowStepsRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
                .workerId = workerId,
                .maxResults = 1,
            }
        );

        REQUIRE(pollResponse.steps.size() == 1);
        REQUIRE(pollResponse.steps[0].stepName == "validateOrder");

        return executionId;
    }
};

} // namespace

TEST_CASE("service validate returns valid for well-formed definition")
{
    TestContext context;

    const auto response = context.service.validateWorkflowDefinition(
        ValidateWorkflowDefinitionRequest{
            .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON),
        }
    );

    REQUIRE(response.validation.valid);
    REQUIRE(response.validation.errors.empty());
}

TEST_CASE("service validate returns errors for missing required fields")
{
    TestContext context;

    auto json = workflow::json::Value::object();
    json["workflowName"] = "orderProcessing";

    const auto response = context.service.validateWorkflowDefinition(
        ValidateWorkflowDefinitionRequest{.definitionJson = json}
    );

    REQUIRE_FALSE(response.validation.valid);
    REQUIRE_FALSE(response.validation.errors.empty());
}

TEST_CASE("service validate returns error for non-object JSON")
{
    TestContext context;

    const auto response = context.service.validateWorkflowDefinition(
        ValidateWorkflowDefinitionRequest{
            .definitionJson = workflow::json::parse(R"json(["not","an","object"])json"),
        }
    );

    REQUIRE_FALSE(response.validation.valid);
    REQUIRE_FALSE(response.validation.errors.empty());
}

TEST_CASE("service validate does not register the definition")
{
    TestContext context;

    context.service.validateWorkflowDefinition(
        ValidateWorkflowDefinitionRequest{
            .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON),
        }
    );

    REQUIRE(context.definitionStore.size() == 0);
}

TEST_CASE("service register stores and returns the definition")
{
    TestContext context;

    const auto response = context.service.registerWorkflowDefinition(
        RegisterWorkflowDefinitionRequest{
            .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON),
        }
    );

    REQUIRE(response.definition.workflowName == "orderProcessing");
    REQUIRE(response.definition.workflowVersion == 1);
    REQUIRE(response.definition.startWorkflowStepName == "validateOrder");
    REQUIRE(response.definition.steps.size() == 2);
    REQUIRE(context.definitionStore.size() == 1);
}

TEST_CASE("service register replaces an existing definition")
{
    TestContext context;
    context.registerValidDefinition();

    auto updatedJson = workflow::json::parse(VALID_WORKFLOW_JSON);
    updatedJson["expectedExecutionTime"] = "PT20M";

    context.service.registerWorkflowDefinition(
        RegisterWorkflowDefinitionRequest{.definitionJson = updatedJson}
    );

    const auto stored = context.definitionStore.find("orderProcessing", 1);
    REQUIRE(stored.has_value());
    REQUIRE(stored->expectedExecutionTime == "PT20M");
    REQUIRE(context.definitionStore.size() == 1);
}

TEST_CASE("service register throws for invalid definition JSON")
{
    TestContext context;

    auto invalidJson = workflow::json::Value::object();
    invalidJson["workflowName"] = "orderProcessing";

    REQUIRE_THROWS_AS(
        context.service.registerWorkflowDefinition(
            RegisterWorkflowDefinitionRequest{.definitionJson = invalidJson}
        ),
        std::invalid_argument
    );
}

TEST_CASE("service start throws when definition is not registered")
{
    TestContext context;

    REQUIRE_THROWS_AS(
        context.service.startWorkflowExecution(
            StartWorkflowExecutionRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service start creates a running execution with the initial step pending")
{
    TestContext context;
    context.registerValidDefinition();

    const auto response = context.service.startWorkflowExecution(
        StartWorkflowExecutionRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .input = workflow::json::Value::object(),
        }
    );

    REQUIRE(response.execution.workflowName == "orderProcessing");
    REQUIRE(response.execution.workflowVersion == 1);
    REQUIRE(response.execution.status == WorkflowExecutionStatus::Running);
    REQUIRE(response.execution.currentStepName == "validateOrder");
    REQUIRE(response.execution.currentStepAttempt == 0);

    const auto stepExecution =
        context.stepExecutionStore.find(response.execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(stepExecution.has_value());
    REQUIRE(stepExecution->status == StepExecutionStatus::Pending);
}

TEST_CASE("service poll-and-claim returns empty when no steps are pending")
{
    TestContext context;
    context.registerValidDefinition();

    const auto response = context.service.pollAndClaimWorkflowSteps(
        PollAndClaimWorkflowStepsRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .workerId = "worker-001",
            .maxResults = 1,
        }
    );

    REQUIRE(response.steps.empty());
}

TEST_CASE("service poll-and-claim claims the initial step with a lease")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.startExecution();

    const auto response = context.service.pollAndClaimWorkflowSteps(
        PollAndClaimWorkflowStepsRequest{
            .workflowName = "orderProcessing",
            .workflowVersion = 1,
            .workerId = "worker-001",
            .maxResults = 1,
        }
    );

    REQUIRE(response.steps.size() == 1);
    REQUIRE(response.steps[0].workflowExecutionId == executionId);
    REQUIRE(response.steps[0].stepName == "validateOrder");
    REQUIRE(response.steps[0].status == StepExecutionStatus::Running);
    REQUIRE(response.steps[0].workerId.has_value());
    REQUIRE(response.steps[0].workerId.value() == "worker-001");
    REQUIRE(response.steps[0].leaseExpiresAt.has_value());
    REQUIRE(response.steps[0].leaseExpiresAt.value() > std::chrono::system_clock::now());
}

TEST_CASE("service poll-and-claim throws when definition is not registered")
{
    TestContext context;

    REQUIRE_THROWS_AS(
        context.service.pollAndClaimWorkflowSteps(
            PollAndClaimWorkflowStepsRequest{
                .workflowName = "orderProcessing",
                .workflowVersion = 1,
                .workerId = "worker-001",
                .maxResults = 1,
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service keep-alive extends the claimed step lease")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    const auto before =
        context.stepExecutionStore.find(executionId, "validateOrder", 0)->leaseExpiresAt;

    const auto response = context.service.keepAliveWorkflowStep(
        KeepAliveWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
        }
    );

    REQUIRE(response.step.status == StepExecutionStatus::Running);
    REQUIRE(response.step.leaseExpiresAt.has_value());
    REQUIRE(response.step.leaseExpiresAt.value() > before.value());
}

TEST_CASE("service keep-alive throws for a non-owning worker")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    REQUIRE_THROWS_AS(
        context.service.keepAliveWorkflowStep(
            KeepAliveWorkflowStepRequest{
                .workflowExecutionId = executionId,
                .stepName = "validateOrder",
                .workerId = "worker-002",
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service keep-alive throws for a missing execution")
{
    TestContext context;
    context.registerValidDefinition();

    REQUIRE_THROWS_AS(
        context.service.keepAliveWorkflowStep(
            KeepAliveWorkflowStepRequest{
                .workflowExecutionId = "nonexistent-id",
                .stepName = "validateOrder",
                .workerId = "worker-001",
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service complete advances execution to the next step")
{
    TestContext context({nextStepDecision("chargePayment")});
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    workflow::json::Value output = workflow::json::Value::object();
    output["valid"] = true;

    const auto response = context.service.completeWorkflowStep(
        CompleteWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
            .stepOutput = output,
        }
    );

    REQUIRE(response.execution.status == WorkflowExecutionStatus::Running);
    REQUIRE(response.execution.currentStepName == "chargePayment");
    REQUIRE(response.execution.currentStepAttempt == 0);

    const auto completedStep = context.stepExecutionStore.find(executionId, "validateOrder", 0);
    REQUIRE(completedStep.has_value());
    REQUIRE(completedStep->status == StepExecutionStatus::Completed);

    const auto nextStep = context.stepExecutionStore.find(executionId, "chargePayment", 0);
    REQUIRE(nextStep.has_value());
    REQUIRE(nextStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("service complete marks workflow completed when logic returns complete decision")
{
    TestContext context({completeWorkflowDecision()});
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    const auto response = context.service.completeWorkflowStep(
        CompleteWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
            .stepOutput = workflow::json::Value::object(),
        }
    );

    REQUIRE(response.execution.status == WorkflowExecutionStatus::Completed);

    const auto stored = context.executionStore.find(executionId);
    REQUIRE(stored.has_value());
    REQUIRE(stored->status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("service complete throws for a non-owning worker")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    REQUIRE_THROWS_AS(
        context.service.completeWorkflowStep(
            CompleteWorkflowStepRequest{
                .workflowExecutionId = executionId,
                .stepName = "validateOrder",
                .workerId = "worker-002",
                .stepOutput = workflow::json::Value::object(),
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service complete throws after lease expiry")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    auto step = context.stepExecutionStore.find(executionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    step->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.stepExecutionStore.update(*step);

    REQUIRE_THROWS_AS(
        context.service.completeWorkflowStep(
            CompleteWorkflowStepRequest{
                .workflowExecutionId = executionId,
                .stepName = "validateOrder",
                .workerId = "worker-001",
                .stepOutput = workflow::json::Value::object(),
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service fail creates a retry step when retries remain")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    const auto response = context.service.failWorkflowStep(
        FailWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
            .reason = "upstream timeout",
        }
    );

    REQUIRE(response.execution.status == WorkflowExecutionStatus::Running);
    REQUIRE(response.execution.currentStepName == "validateOrder");
    REQUIRE(response.execution.currentStepAttempt == 1);

    const auto failedStep = context.stepExecutionStore.find(executionId, "validateOrder", 0);
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
    REQUIRE(failedStep->failureReason.value() == "upstream timeout");

    const auto retryStep = context.stepExecutionStore.find(executionId, "validateOrder", 1);
    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("service fail marks workflow failed when max retries are exceeded")
{
    TestContext context;

    const auto noRetryJson = workflow::json::parse(R"json(
    {
      "workflowName": "orderProcessing",
      "workflowVersion": 1,
      "startWorkflowStepName": "validateOrder",
      "expectedExecutionTime": "PT10M",
      "steps": [
        {
          "name": "validateOrder",
          "expectedExecutionTime": "PT30S",
          "maxRetries": 0
        }
      ]
    }
    )json");

    context.service.registerWorkflowDefinition(
        RegisterWorkflowDefinitionRequest{.definitionJson = noRetryJson}
    );

    const auto executionId = context.claimInitialStep("worker-001");

    const auto response = context.service.failWorkflowStep(
        FailWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
            .reason = "permanent failure",
        }
    );

    REQUIRE(response.execution.status == WorkflowExecutionStatus::Failed);
    REQUIRE(response.execution.failureReason.has_value());
    REQUIRE(response.execution.failureReason.value() == "permanent failure");

    const auto stored = context.executionStore.find(executionId);
    REQUIRE(stored.has_value());
    REQUIRE(stored->status == WorkflowExecutionStatus::Failed);
}

TEST_CASE("service fail throws for a non-owning worker")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    REQUIRE_THROWS_AS(
        context.service.failWorkflowStep(
            FailWorkflowStepRequest{
                .workflowExecutionId = executionId,
                .stepName = "validateOrder",
                .workerId = "worker-002",
                .reason = "timeout",
            }
        ),
        std::runtime_error
    );
}

TEST_CASE("service get-workflow-execution returns nullopt for unknown id")
{
    TestContext context;

    const auto response = context.service.getWorkflowExecution(
        GetWorkflowExecutionRequest{.workflowExecutionId = "nonexistent-id"}
    );

    REQUIRE_FALSE(response.execution.has_value());
}

TEST_CASE("service get-workflow-execution returns execution after start")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.startExecution();

    const auto response = context.service.getWorkflowExecution(
        GetWorkflowExecutionRequest{.workflowExecutionId = executionId}
    );

    REQUIRE(response.execution.has_value());
    REQUIRE(response.execution->workflowExecutionId == executionId);
    REQUIRE(response.execution->workflowName == "orderProcessing");
    REQUIRE(response.execution->status == WorkflowExecutionStatus::Running);
    REQUIRE(response.execution->currentStepName == "validateOrder");
}

TEST_CASE("service get-workflow-execution reflects status after workflow completes")
{
    TestContext context({completeWorkflowDecision()});
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    context.service.completeWorkflowStep(
        CompleteWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
            .stepOutput = workflow::json::Value::object(),
        }
    );

    const auto response = context.service.getWorkflowExecution(
        GetWorkflowExecutionRequest{.workflowExecutionId = executionId}
    );

    REQUIRE(response.execution.has_value());
    REQUIRE(response.execution->status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("service get-workflow-execution throws for empty id")
{
    TestContext context;

    REQUIRE_THROWS_AS(
        context.service.getWorkflowExecution(
            GetWorkflowExecutionRequest{.workflowExecutionId = ""}
        ),
        std::invalid_argument
    );
}

TEST_CASE("service list-workflow-definitions returns empty before any registration")
{
    TestContext context;

    const auto response = context.service.listWorkflowDefinitions(ListWorkflowDefinitionsRequest{});

    REQUIRE(response.definitions.empty());
}

TEST_CASE("service list-workflow-definitions returns all registered definitions in key order")
{
    TestContext context;

    context.service.registerWorkflowDefinition(
        RegisterWorkflowDefinitionRequest{
            .definitionJson = workflow::json::parse(VALID_WORKFLOW_JSON),
        }
    );

    const auto secondJson = workflow::json::parse(R"json(
    {
      "workflowName": "invoiceProcessing",
      "workflowVersion": 1,
      "startWorkflowStepName": "generateInvoice",
      "expectedExecutionTime": "PT5M",
      "steps": [
        {
          "name": "generateInvoice",
          "expectedExecutionTime": "PT1M",
          "maxRetries": 0
        }
      ]
    }
    )json");

    context.service.registerWorkflowDefinition(
        RegisterWorkflowDefinitionRequest{.definitionJson = secondJson}
    );

    const auto response = context.service.listWorkflowDefinitions(ListWorkflowDefinitionsRequest{});

    REQUIRE(response.definitions.size() == 2);
    REQUIRE(response.definitions[0].workflowName == "invoiceProcessing");
    REQUIRE(response.definitions[0].workflowVersion == 1);
    REQUIRE(response.definitions[1].workflowName == "orderProcessing");
    REQUIRE(response.definitions[1].workflowVersion == 1);
}

TEST_CASE("service list-workflow-definitions reflects re-registration without duplication")
{
    TestContext context;
    context.registerValidDefinition();
    context.registerValidDefinition();

    const auto response = context.service.listWorkflowDefinitions(ListWorkflowDefinitionsRequest{});

    REQUIRE(response.definitions.size() == 1);
}

TEST_CASE("service cancel marks workflow canceled and cancels pending step")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.startExecution();

    const auto response =
        context.service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = executionId});

    REQUIRE(response.execution.status == WorkflowExecutionStatus::Canceled);
    REQUIRE(response.execution.workflowExecutionId == executionId);

    const auto stored = context.executionStore.find(executionId);
    REQUIRE(stored.has_value());
    REQUIRE(stored->status == WorkflowExecutionStatus::Canceled);

    const auto step = context.stepExecutionStore.find(executionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->status == StepExecutionStatus::Canceled);
}

TEST_CASE("service cancel marks workflow canceled and cancels running step")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    const auto response =
        context.service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = executionId});

    REQUIRE(response.execution.status == WorkflowExecutionStatus::Canceled);

    const auto step = context.stepExecutionStore.find(executionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->status == StepExecutionStatus::Canceled);
    REQUIRE_FALSE(step->workerId.has_value());
    REQUIRE_FALSE(step->leaseExpiresAt.has_value());
}

TEST_CASE("service cancel throws for an unknown execution id")
{
    TestContext context;

    REQUIRE_THROWS_AS(
        context.service.cancelWorkflow(
            CancelWorkflowRequest{.workflowExecutionId = "nonexistent-id"}
        ),
        std::runtime_error
    );
}

TEST_CASE("service cancel throws when execution is already completed")
{
    TestContext context({completeWorkflowDecision()});
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    context.service.completeWorkflowStep(
        CompleteWorkflowStepRequest{
            .workflowExecutionId = executionId,
            .stepName = "validateOrder",
            .workerId = "worker-001",
            .stepOutput = workflow::json::Value::object(),
        }
    );

    REQUIRE_THROWS_AS(
        context.service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = executionId}),
        std::runtime_error
    );
}

TEST_CASE("service cancel throws when execution is already canceled")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.startExecution();

    context.service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = executionId});

    REQUIRE_THROWS_AS(
        context.service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = executionId}),
        std::runtime_error
    );
}

TEST_CASE("service cancel throws for an empty execution id")
{
    TestContext context;

    REQUIRE_THROWS_AS(
        context.service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = ""}),
        std::invalid_argument
    );
}

TEST_CASE("service sweep-expired-leases retries expired step and returns retriedCount 1")
{
    TestContext context;
    context.registerValidDefinition();
    const auto executionId = context.claimInitialStep("worker-001");

    auto step = context.stepExecutionStore.find(executionId, "validateOrder", 0).value();
    step.leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.stepExecutionStore.update(step);

    const auto response = context.service.sweepExpiredLeases(SweepExpiredLeasesRequest{});

    REQUIRE(response.result.retriedCount == 1);
    REQUIRE(response.result.failedCount == 0);
}

TEST_CASE("service sweep-expired-leases returns {0,0} when no expired steps exist")
{
    TestContext context;
    context.registerValidDefinition();
    context.startExecution();

    const auto response = context.service.sweepExpiredLeases(SweepExpiredLeasesRequest{});

    REQUIRE(response.result.retriedCount == 0);
    REQUIRE(response.result.failedCount == 0);
}
