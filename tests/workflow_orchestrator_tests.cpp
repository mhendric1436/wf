#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"
#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_orchestrator.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using workflow::NextStepDecision;
using workflow::StepCompletionContext;
using workflow::StepExecutionStatus;
using workflow::WorkflowDefinition;
using workflow::WorkflowExecution;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowLogic;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowStep;
using workflow::WorkflowStepExecution;
using workflow::backend::memory::InMemoryWorkflowDefinitionStore;
using workflow::backend::memory::InMemoryWorkflowExecutionStore;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;

namespace
{

WorkflowDefinition makeWorkflowDefinition(int validateOrderMaxRetries = 2)
{
    WorkflowDefinition definition;
    definition.workflowName = "orderProcessing";
    definition.workflowVersion = 1;
    definition.startWorkflowStepName = "validateOrder";
    definition.expectedExecutionTime = "PT10M";
    definition.steps = {
        WorkflowStep{
            .name = "validateOrder",
            .expectedExecutionTime = "PT30S",
            .maxRetries = validateOrderMaxRetries,
        },
        WorkflowStep{
            .name = "chargePayment",
            .expectedExecutionTime = "PT2M",
            .maxRetries = 1,
        },
    };
    return definition;
}

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

    NextStepDecision decideNextStep(const StepCompletionContext& context) override
    {
        lastContext = context;
        ++callCount;

        if (nextDecisionIndex_ >= decisions_.size())
        {
            return completeWorkflowDecision();
        }

        return decisions_.at(nextDecisionIndex_++);
    }

    int callCount = 0;
    std::optional<StepCompletionContext> lastContext;

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

    explicit TestContext(
        WorkflowDefinition definition = makeWorkflowDefinition(),
        std::vector<NextStepDecision> decisions = {}
    )
        : logic(std::move(decisions)),
          orchestrator(
              definitionStore,
              executionStore,
              stepExecutionStore,
              logic
          )
    {
        definitionStore.save(definition);
    }
};

WorkflowExecution startWorkflow(TestContext& context)
{
    return context.orchestrator.startWorkflow(
        "orderProcessing", 1, workflow::json::Value::object()
    );
}

std::vector<WorkflowStepExecution> pollAndClaim(
    TestContext& context,
    const std::string& workerId = "worker-001",
    std::size_t maxResults = 1
)
{
    return context.orchestrator.pollAndClaimWorkflowSteps(
        "orderProcessing", 1, workerId, maxResults
    );
}

WorkflowStepExecution claimStartStep(
    TestContext& context,
    const std::string& workerId = "worker-001"
)
{
    const auto claimed = pollAndClaim(context, workerId);

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].stepName == "validateOrder");

    return claimed[0];
}

} // namespace

TEST_CASE("orchestrator start creates workflow execution and initial pending step")
{
    TestContext context;

    const auto execution = startWorkflow(context);

    REQUIRE(execution.workflowName == "orderProcessing");
    REQUIRE(execution.workflowVersion == 1);
    REQUIRE(execution.status == WorkflowExecutionStatus::Running);
    REQUIRE(execution.currentStepName == "validateOrder");
    REQUIRE(execution.currentStepAttempt == 0);

    const auto storedExecution = context.executionStore.find(execution.workflowExecutionId);

    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->currentStepName == "validateOrder");

    const auto stepExecution =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(stepExecution.has_value());
    REQUIRE(stepExecution->workflowExecutionId == execution.workflowExecutionId);
    REQUIRE(stepExecution->workflowName == "orderProcessing");
    REQUIRE(stepExecution->workflowVersion == 1);
    REQUIRE(stepExecution->stepName == "validateOrder");
    REQUIRE(stepExecution->attempt == 0);
    REQUIRE(stepExecution->status == StepExecutionStatus::Pending);
}

TEST_CASE("orchestrator poll-and-claim claims a pending step with a lease")
{
    TestContext context;
    const auto execution = startWorkflow(context);

    const auto claimed = pollAndClaim(context, "worker-001");

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].workflowExecutionId == execution.workflowExecutionId);
    REQUIRE(claimed[0].stepName == "validateOrder");
    REQUIRE(claimed[0].status == StepExecutionStatus::Claimed);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
    REQUIRE(claimed[0].leaseExpiresAt.value() > std::chrono::system_clock::now());

    const auto storedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(storedStep.has_value());
    REQUIRE(storedStep->status == StepExecutionStatus::Claimed);
    REQUIRE(storedStep->workerId.value() == "worker-001");
    REQUIRE(storedStep->leaseExpiresAt.has_value());
}

TEST_CASE("orchestrator keep-alive extends the claimed step lease")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    const auto claimed = claimStartStep(context, "worker-001");

    REQUIRE(claimed.leaseExpiresAt.has_value());
    const auto originalLeaseExpiresAt = claimed.leaseExpiresAt.value();

    const auto keptAlive = context.orchestrator.keepAliveStep(
        execution.workflowExecutionId, "validateOrder", "worker-001"
    );

    REQUIRE(keptAlive.status == StepExecutionStatus::Claimed);
    REQUIRE(keptAlive.workerId.has_value());
    REQUIRE(keptAlive.workerId.value() == "worker-001");
    REQUIRE(keptAlive.leaseExpiresAt.has_value());
    REQUIRE(keptAlive.leaseExpiresAt.value() > originalLeaseExpiresAt);
}

TEST_CASE("orchestrator complete step with active lease advances to next pending step")
{
    TestContext context(makeWorkflowDefinition(), {nextStepDecision("chargePayment")});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    workflow::json::Value stepOutput = workflow::json::Value::object();
    stepOutput["valid"] = true;

    const auto updatedExecution = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", stepOutput
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Running);
    REQUIRE(updatedExecution.currentStepName == "chargePayment");
    REQUIRE(updatedExecution.currentStepAttempt == 0);
    REQUIRE(context.logic.callCount == 1);
    REQUIRE(context.logic.lastContext.has_value());
    REQUIRE(context.logic.lastContext->completedStepName == "validateOrder");

    const auto completedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(completedStep.has_value());
    REQUIRE(completedStep->status == StepExecutionStatus::Completed);
    REQUIRE(completedStep->output.contains("valid"));
    REQUIRE(completedStep->output.at("valid").asBool());

    const auto nextStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "chargePayment", 0);

    REQUIRE(nextStep.has_value());
    REQUIRE(nextStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("orchestrator rejects complete after lease expiry")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    auto claimedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(claimedStep.has_value());

    claimedStep->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.stepExecutionStore.update(*claimedStep);

    REQUIRE_THROWS_AS(
        context.orchestrator.completeStep(
            execution.workflowExecutionId, "validateOrder", "worker-001",
            workflow::json::Value::object()
        ),
        std::runtime_error
    );
}

TEST_CASE("orchestrator fail step with active lease creates retry pending step")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    const auto updatedExecution = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "validation service timeout"
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Running);
    REQUIRE(updatedExecution.currentStepName == "validateOrder");
    REQUIRE(updatedExecution.currentStepAttempt == 1);

    const auto failedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
    REQUIRE(failedStep->failureReason.has_value());
    REQUIRE(failedStep->failureReason.value() == "validation service timeout");

    const auto retryStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 1);

    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("orchestrator max retries exceeded marks workflow failed")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    const auto updatedExecution = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "validation failed"
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Failed);
    REQUIRE(updatedExecution.failureReason.has_value());
    REQUIRE(updatedExecution.failureReason.value() == "validation failed");

    const auto storedExecution = context.executionStore.find(execution.workflowExecutionId);

    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->status == WorkflowExecutionStatus::Failed);

    const auto failedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
}

TEST_CASE("orchestrator completes workflow when workflow logic returns complete decision")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    const auto updatedExecution = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001",
        workflow::json::Value::object()
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Completed);
    REQUIRE(context.logic.callCount == 1);

    const auto storedExecution = context.executionStore.find(execution.workflowExecutionId);

    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("orchestrator rejects complete from non-owning worker")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    REQUIRE_THROWS_AS(
        context.orchestrator.completeStep(
            execution.workflowExecutionId, "validateOrder", "worker-002",
            workflow::json::Value::object()
        ),
        std::runtime_error
    );
}

TEST_CASE("orchestrator rejects keep-alive from non-owning worker")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    REQUIRE_THROWS_AS(
        context.orchestrator.keepAliveStep(
            execution.workflowExecutionId, "validateOrder", "worker-002"
        ),
        std::runtime_error
    );
}
