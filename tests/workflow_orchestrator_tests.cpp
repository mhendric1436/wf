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
#include <thread>
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
    REQUIRE(claimed[0].status == StepExecutionStatus::Running);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
    REQUIRE(claimed[0].leaseExpiresAt.value() > std::chrono::system_clock::now());

    const auto storedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);

    REQUIRE(storedStep.has_value());
    REQUIRE(storedStep->status == StepExecutionStatus::Running);
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

    REQUIRE(keptAlive.status == StepExecutionStatus::Running);
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

namespace
{

void expireLease(
    TestContext& context,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    auto step = context.stepExecutionStore.find(workflowExecutionId, stepName, attempt).value();
    step.leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.stepExecutionStore.update(step);
}

} // namespace

TEST_CASE("sweepExpiredLeases returns {0,0} when no expired steps exist")
{
    TestContext context;
    startWorkflow(context);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 0);
}

TEST_CASE("sweepExpiredLeases retries expired step when attempts remain")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 1);
    REQUIRE(result.failedCount == 0);

    const auto failedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
    REQUIRE(failedStep->failureReason == "lease expired");
    REQUIRE_FALSE(failedStep->leaseExpiresAt.has_value());

    const auto retryStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 1);
    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);

    const auto updatedExecution = context.executionStore.find(execution.workflowExecutionId);
    REQUIRE(updatedExecution.has_value());
    REQUIRE(updatedExecution->status == WorkflowExecutionStatus::Running);
    REQUIRE(updatedExecution->currentStepAttempt == 1);
}

TEST_CASE("sweepExpiredLeases fails workflow when no retries remain")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 1);

    const auto failedStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);

    const auto updatedExecution = context.executionStore.find(execution.workflowExecutionId);
    REQUIRE(updatedExecution.has_value());
    REQUIRE(updatedExecution->status == WorkflowExecutionStatus::Failed);
    REQUIRE(updatedExecution->failureReason == "lease expired");
}

TEST_CASE("sweepExpiredLeases is idempotent on a second call after retry")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    context.orchestrator.sweepExpiredLeases();

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 0);
}

TEST_CASE("sweepExpiredLeases skips step whose workflow execution is not running")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    auto runningStep =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0).value();
    runningStep.leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.stepExecutionStore.update(runningStep);

    auto canceledExecution = context.executionStore.find(execution.workflowExecutionId).value();
    canceledExecution.status = WorkflowExecutionStatus::Canceled;
    context.executionStore.update(canceledExecution);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 0);
}

TEST_CASE("startWorkflow creates initial step with createdAt set")
{
    TestContext context;
    const auto before = std::chrono::system_clock::now();
    const auto execution = startWorkflow(context);
    const auto after = std::chrono::system_clock::now();

    const auto step =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->createdAt.has_value());
    REQUIRE(step->createdAt.value() >= before);
    REQUIRE(step->createdAt.value() <= after);
}

TEST_CASE("pollAndClaim sets startedAt on claimed step")
{
    TestContext context;
    const auto execution = startWorkflow(context);

    const auto before = std::chrono::system_clock::now();
    claimStartStep(context);
    const auto after = std::chrono::system_clock::now();

    const auto step =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->startedAt.has_value());
    REQUIRE(step->startedAt.value() >= before);
    REQUIRE(step->startedAt.value() <= after);
}

TEST_CASE("completeStep sets completedAt on step execution")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001",
        workflow::json::Value::object()
    );
    const auto after = std::chrono::system_clock::now();

    const auto step =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->completedAt.has_value());
    REQUIRE(step->completedAt.value() >= before);
    REQUIRE(step->completedAt.value() <= after);
}

TEST_CASE("failStep sets completedAt on step execution")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );
    const auto after = std::chrono::system_clock::now();

    const auto step =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->completedAt.has_value());
    REQUIRE(step->completedAt.value() >= before);
    REQUIRE(step->completedAt.value() <= after);
}

TEST_CASE("sweepExpiredLeases sets completedAt on the failed step")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.sweepExpiredLeases();
    const auto after = std::chrono::system_clock::now();

    const auto step =
        context.stepExecutionStore.find(execution.workflowExecutionId, "validateOrder", 0);
    REQUIRE(step.has_value());
    REQUIRE(step->completedAt.has_value());
    REQUIRE(step->completedAt.value() >= before);
    REQUIRE(step->completedAt.value() <= after);
}

TEST_CASE("startWorkflow sets startedAt on workflow execution")
{
    TestContext context;

    const auto before = std::chrono::system_clock::now();
    const auto execution = startWorkflow(context);
    const auto after = std::chrono::system_clock::now();

    REQUIRE(execution.startedAt.has_value());
    REQUIRE(execution.startedAt.value() >= before);
    REQUIRE(execution.startedAt.value() <= after);

    const auto stored = context.executionStore.find(execution.workflowExecutionId);
    REQUIRE(stored->startedAt.has_value());
    REQUIRE(stored->startedAt.value() == execution.startedAt.value());
}

TEST_CASE("completeStep sets completedAt on workflow execution when workflow completes")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001",
        workflow::json::Value::object()
    );
    const auto after = std::chrono::system_clock::now();

    REQUIRE(result.completedAt.has_value());
    REQUIRE(result.completedAt.value() >= before);
    REQUIRE(result.completedAt.value() <= after);
}

TEST_CASE("completeStep does not set completedAt when workflow continues to next step")
{
    TestContext context(makeWorkflowDefinition(), {nextStepDecision("chargePayment")});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001",
        workflow::json::Value::object()
    );

    REQUIRE_FALSE(result.completedAt.has_value());
}

TEST_CASE("failStep sets completedAt on workflow execution when retries are exhausted")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );
    const auto after = std::chrono::system_clock::now();

    REQUIRE(result.status == WorkflowExecutionStatus::Failed);
    REQUIRE(result.completedAt.has_value());
    REQUIRE(result.completedAt.value() >= before);
    REQUIRE(result.completedAt.value() <= after);
}

TEST_CASE("failStep does not set completedAt when retries remain")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto result = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );

    REQUIRE(result.status == WorkflowExecutionStatus::Running);
    REQUIRE_FALSE(result.completedAt.has_value());
}

TEST_CASE("cancelWorkflow sets completedAt on workflow execution")
{
    TestContext context;
    const auto execution = startWorkflow(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.cancelWorkflow(execution.workflowExecutionId);
    const auto after = std::chrono::system_clock::now();

    REQUIRE(result.completedAt.has_value());
    REQUIRE(result.completedAt.value() >= before);
    REQUIRE(result.completedAt.value() <= after);
}

TEST_CASE("sweepExpiredLeases sets completedAt on workflow execution when failed")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.sweepExpiredLeases();
    const auto after = std::chrono::system_clock::now();

    const auto stored = context.executionStore.find(execution.workflowExecutionId);
    REQUIRE(stored->completedAt.has_value());
    REQUIRE(stored->completedAt.value() >= before);
    REQUIRE(stored->completedAt.value() <= after);
}

TEST_CASE("independent workflow executions can be completed concurrently without deadlock")
{
    TestContext context(
        makeWorkflowDefinition(), {completeWorkflowDecision(), completeWorkflowDecision()}
    );

    const auto execA = startWorkflow(context);
    const auto execB = startWorkflow(context);

    const auto stepsA = pollAndClaim(context, "worker-A");
    const auto stepsB = pollAndClaim(context, "worker-B");

    REQUIRE(stepsA.size() == 1);
    REQUIRE(stepsB.size() == 1);

    WorkflowExecution resultA;
    WorkflowExecution resultB;

    std::thread threadA(
        [&]()
        {
            resultA = context.orchestrator.completeStep(
                execA.workflowExecutionId, "validateOrder", "worker-A",
                workflow::json::Value::object()
            );
        }
    );

    std::thread threadB(
        [&]()
        {
            resultB = context.orchestrator.completeStep(
                execB.workflowExecutionId, "validateOrder", "worker-B",
                workflow::json::Value::object()
            );
        }
    );

    threadA.join();
    threadB.join();

    REQUIRE(resultA.status == WorkflowExecutionStatus::Completed);
    REQUIRE(resultB.status == WorkflowExecutionStatus::Completed);
}
