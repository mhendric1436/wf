#include "wf/workflow_orchestrator.hpp"

#include "wf/duration.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <stdexcept>

namespace workflow
{
namespace
{

const WorkflowStep& findStepDefinition(
    const WorkflowDefinition& definition,
    const std::string& stepName
)
{
    const auto iter = std::find_if(
        definition.steps.begin(), definition.steps.end(),
        [&stepName](const WorkflowStep& step) { return step.name == stepName; }
    );

    if (iter == definition.steps.end())
    {
        throw std::runtime_error("workflow step is not defined: " + stepName);
    }

    return *iter;
}

bool stepDefinitionExists(
    const WorkflowDefinition& definition,
    const std::string& stepName
)
{
    return std::any_of(
        definition.steps.begin(), definition.steps.end(),
        [&stepName](const WorkflowStep& step) { return step.name == stepName; }
    );
}

int maxRetriesForStep(const WorkflowStep& step)
{
    return step.maxRetries.value_or(0);
}

void validateWorkflowNameAndVersion(
    const std::string& workflowName,
    int workflowVersion
)
{
    if (workflowName.empty())
    {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (workflowVersion < 1)
    {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }
}

void validateExecutionId(const std::string& workflowExecutionId)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }
}

void validateWorkerId(const std::string& workerId)
{
    if (workerId.empty())
    {
        throw std::invalid_argument("workerId must not be empty");
    }
}

void validateStepName(const std::string& stepName)
{
    if (stepName.empty())
    {
        throw std::invalid_argument("stepName must not be empty");
    }
}

void validateExecutionIsRunning(const WorkflowExecution& execution)
{
    if (execution.status != WorkflowExecutionStatus::Running)
    {
        throw std::runtime_error(
            "workflow execution is not running: " + execution.workflowExecutionId
        );
    }
}

void validateClaimOwnership(
    const WorkflowStepExecution& stepExecution,
    const std::string& workerId
)
{
    const auto now = std::chrono::system_clock::now();

    if (stepExecution.status != StepExecutionStatus::Running)
    {
        throw std::runtime_error(
            "workflow step execution is not running: " + stepExecution.stepName
        );
    }

    if (!stepExecution.workerId.has_value())
    {
        throw std::runtime_error(
            "workflow step execution does not have a worker claim: " + stepExecution.stepName
        );
    }

    if (stepExecution.workerId.value() != workerId)
    {
        throw std::runtime_error(
            "workflow step execution is owned by a different worker: " + stepExecution.stepName
        );
    }

    if (!stepExecution.leaseExpiresAt.has_value())
    {
        throw std::runtime_error(
            "workflow step execution does not have an active lease: " + stepExecution.stepName
        );
    }

    if (stepExecution.leaseExpiresAt.value() <= now)
    {
        throw std::runtime_error(
            "workflow step execution lease has expired: " + stepExecution.stepName
        );
    }
}

std::chrono::seconds leaseDurationForStep(const WorkflowStep& step)
{
    if (!step.expectedExecutionTime.has_value())
    {
        throw std::runtime_error(
            "step expectedExecutionTime is required for lease calculation: " + step.name
        );
    }

    return calculateLeaseDuration(step.expectedExecutionTime.value());
}

std::map<
    std::string,
    std::chrono::seconds>
buildLeaseDurationsByStepName(const WorkflowDefinition& definition)
{
    std::map<std::string, std::chrono::seconds> durations;

    for (const auto& step : definition.steps)
    {
        durations[step.name] = leaseDurationForStep(step);
    }

    return durations;
}

WorkflowStepExecution makeStepExecution(
    const WorkflowExecution& execution,
    const std::string& stepName,
    int attempt
)
{
    WorkflowStepExecution stepExecution;
    stepExecution.workflowExecutionId = execution.workflowExecutionId;
    stepExecution.workflowName = execution.workflowName;
    stepExecution.workflowVersion = execution.workflowVersion;
    stepExecution.stepName = stepName;
    stepExecution.attempt = attempt;
    stepExecution.status = StepExecutionStatus::Pending;
    stepExecution.input = execution.input;
    stepExecution.state = execution.state;
    stepExecution.output = json::Value::object();
    stepExecution.createdAt = std::chrono::system_clock::now();
    return stepExecution;
}

} // namespace

WorkflowOrchestrator::WorkflowOrchestrator(
    WorkflowDefinitionStore& definitionStore,
    WorkflowExecutionStore& executionStore,
    WorkflowStepExecutionStore& stepExecutionStore,
    WorkflowLogic& workflowLogic
)
    : definitionStore_(definitionStore),
      executionStore_(executionStore),
      stepExecutionStore_(stepExecutionStore),
      workflowLogic_(workflowLogic)
{
}

std::mutex& WorkflowOrchestrator::stripeFor(const std::string& executionId) const
{
    return executionStripes_[std::hash<std::string>{}(executionId) % STRIPE_COUNT];
}

WorkflowExecution WorkflowOrchestrator::startWorkflow(
    const std::string& workflowName,
    int workflowVersion,
    const json::Value& input
)
{
    validateWorkflowNameAndVersion(workflowName, workflowVersion);

    const auto definition = definitionStore_.find(workflowName, workflowVersion);

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + workflowName);
    }

    WorkflowExecution execution;
    execution.workflowExecutionId = executionStore_.generateExecutionId();
    execution.workflowName = workflowName;
    execution.workflowVersion = workflowVersion;
    execution.status = WorkflowExecutionStatus::Running;
    execution.currentStepName = definition->startWorkflowStepName;
    execution.input = input;
    execution.state = json::Value::object();
    execution.currentStepAttempt = 0;
    execution.startedAt = std::chrono::system_clock::now();

    executionStore_.save(execution);

    stepExecutionStore_.save(makeStepExecution(execution, definition->startWorkflowStepName, 0));

    return execution;
}

std::vector<WorkflowStepExecution> WorkflowOrchestrator::pollAndClaimWorkflowSteps(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults
)
{
    validateWorkflowNameAndVersion(workflowName, workflowVersion);
    validateWorkerId(workerId);

    if (maxResults == 0)
    {
        throw std::invalid_argument("maxResults must be greater than 0");
    }

    const auto definition = definitionStore_.find(workflowName, workflowVersion);

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + workflowName);
    }

    const auto leaseDurationsByStepName = buildLeaseDurationsByStepName(*definition);

    return stepExecutionStore_.pollAndClaim(
        workflowName, workflowVersion, workerId, maxResults, leaseDurationsByStepName
    );
}

WorkflowStepExecution WorkflowOrchestrator::keepAliveStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId
)
{
    std::lock_guard<std::mutex> lock(stripeFor(workflowExecutionId));
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);
    validateWorkerId(workerId);

    const auto execution = executionStore_.find(workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    validateExecutionIsRunning(*execution);

    if (execution->currentStepName != stepName)
    {
        throw std::runtime_error("step does not match current workflow step: " + stepName);
    }

    const auto definition =
        definitionStore_.find(execution->workflowName, execution->workflowVersion);

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + execution->workflowName);
    }

    const auto& stepDefinition = findStepDefinition(*definition, stepName);
    const auto leaseDuration = leaseDurationForStep(stepDefinition);

    return stepExecutionStore_.keepAlive(
        workflowExecutionId, stepName, execution->currentStepAttempt, workerId, leaseDuration
    );
}

WorkflowExecution WorkflowOrchestrator::completeStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId,
    const json::Value& stepOutput
)
{
    std::lock_guard<std::mutex> lock(stripeFor(workflowExecutionId));
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);
    validateWorkerId(workerId);

    const auto execution = executionStore_.find(workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    WorkflowExecution updatedExecution = *execution;
    validateExecutionIsRunning(updatedExecution);

    if (updatedExecution.currentStepName != stepName)
    {
        throw std::runtime_error("step does not match current workflow step: " + stepName);
    }

    const auto stepExecution = stepExecutionStore_.find(
        workflowExecutionId, stepName, updatedExecution.currentStepAttempt
    );

    if (!stepExecution.has_value())
    {
        throw std::runtime_error("workflow step execution not found: " + stepName);
    }

    WorkflowStepExecution updatedStepExecution = *stepExecution;
    validateClaimOwnership(updatedStepExecution, workerId);

    const auto definition =
        definitionStore_.find(updatedExecution.workflowName, updatedExecution.workflowVersion);

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + updatedExecution.workflowName);
    }

    updatedStepExecution.status = StepExecutionStatus::Completed;
    updatedStepExecution.output = stepOutput;
    updatedStepExecution.leaseExpiresAt.reset();
    updatedStepExecution.completedAt = std::chrono::system_clock::now();
    stepExecutionStore_.update(updatedStepExecution);

    StepCompletionContext context;
    context.workflowName = updatedExecution.workflowName;
    context.workflowVersion = updatedExecution.workflowVersion;
    context.workflowExecutionId = updatedExecution.workflowExecutionId;
    context.completedStepName = stepName;
    context.input = updatedExecution.input;
    context.state = updatedExecution.state;
    context.stepOutput = stepOutput;

    const auto decision = workflowLogic_.decideNextStep(context);

    updatedExecution.state = decision.updatedState;

    if (decision.workflowComplete)
    {
        updatedExecution.status = WorkflowExecutionStatus::Completed;
        updatedExecution.completedAt = std::chrono::system_clock::now();
        executionStore_.update(updatedExecution);
        return updatedExecution;
    }

    if (!decision.nextStepName.has_value())
    {
        throw std::runtime_error(
            "next step decision must provide nextStepName or workflowComplete"
        );
    }

    const auto nextStepName = decision.nextStepName.value();

    if (!stepDefinitionExists(*definition, nextStepName))
    {
        throw std::runtime_error("next step is not defined: " + nextStepName);
    }

    updatedExecution.currentStepName = nextStepName;
    updatedExecution.currentStepAttempt = 0;

    executionStore_.update(updatedExecution);

    stepExecutionStore_.save(makeStepExecution(updatedExecution, nextStepName, 0));

    return updatedExecution;
}

WorkflowExecution WorkflowOrchestrator::failStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId,
    const std::string& reason
)
{
    std::lock_guard<std::mutex> lock(stripeFor(workflowExecutionId));
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);
    validateWorkerId(workerId);

    const auto execution = executionStore_.find(workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    WorkflowExecution updatedExecution = *execution;
    validateExecutionIsRunning(updatedExecution);

    if (updatedExecution.currentStepName != stepName)
    {
        throw std::runtime_error("step does not match current workflow step: " + stepName);
    }

    const auto stepExecution = stepExecutionStore_.find(
        workflowExecutionId, stepName, updatedExecution.currentStepAttempt
    );

    if (!stepExecution.has_value())
    {
        throw std::runtime_error("workflow step execution not found: " + stepName);
    }

    WorkflowStepExecution updatedStepExecution = *stepExecution;
    validateClaimOwnership(updatedStepExecution, workerId);

    const auto definition =
        definitionStore_.find(updatedExecution.workflowName, updatedExecution.workflowVersion);

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + updatedExecution.workflowName);
    }

    const auto& stepDefinition = findStepDefinition(*definition, stepName);
    const int maxRetries = maxRetriesForStep(stepDefinition);

    updatedStepExecution.status = StepExecutionStatus::Failed;
    updatedStepExecution.failureReason = reason;
    updatedStepExecution.leaseExpiresAt.reset();
    updatedStepExecution.completedAt = std::chrono::system_clock::now();
    stepExecutionStore_.update(updatedStepExecution);

    if (updatedExecution.currentStepAttempt < maxRetries)
    {
        ++updatedExecution.currentStepAttempt;

        executionStore_.update(updatedExecution);

        stepExecutionStore_.save(
            makeStepExecution(updatedExecution, stepName, updatedExecution.currentStepAttempt)
        );

        return updatedExecution;
    }

    updatedExecution.status = WorkflowExecutionStatus::Failed;
    updatedExecution.failureReason = reason;
    updatedExecution.completedAt = std::chrono::system_clock::now();
    executionStore_.update(updatedExecution);

    return updatedExecution;
}

WorkflowExecution WorkflowOrchestrator::cancelWorkflow(const std::string& workflowExecutionId)
{
    std::lock_guard<std::mutex> lock(stripeFor(workflowExecutionId));
    validateExecutionId(workflowExecutionId);

    const auto execution = executionStore_.find(workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    if (execution->status != WorkflowExecutionStatus::Running)
    {
        throw std::runtime_error("workflow execution is not running: " + workflowExecutionId);
    }

    stepExecutionStore_.cancelByExecution(workflowExecutionId);

    WorkflowExecution canceled = *execution;
    canceled.status = WorkflowExecutionStatus::Canceled;
    canceled.completedAt = std::chrono::system_clock::now();
    executionStore_.update(canceled);

    return canceled;
}

SweepResult WorkflowOrchestrator::sweepExpiredLeases()
{
    SweepResult sweepResult;

    const auto expiredSteps = stepExecutionStore_.findExpiredRunning();

    for (const auto& expiredStep : expiredSteps)
    {
        std::lock_guard<std::mutex> lock(stripeFor(expiredStep.workflowExecutionId));
        const auto execution = executionStore_.find(expiredStep.workflowExecutionId);

        if (!execution.has_value())
        {
            continue;
        }

        if (execution->status != WorkflowExecutionStatus::Running)
        {
            continue;
        }

        if (execution->currentStepName != expiredStep.stepName ||
            execution->currentStepAttempt != expiredStep.attempt)
        {
            continue;
        }

        const auto definition =
            definitionStore_.find(execution->workflowName, execution->workflowVersion);

        if (!definition.has_value())
        {
            continue;
        }

        const auto& stepDefinition = findStepDefinition(*definition, expiredStep.stepName);
        const int maxRetries = maxRetriesForStep(stepDefinition);

        WorkflowStepExecution failedStep = expiredStep;
        failedStep.status = StepExecutionStatus::Failed;
        failedStep.failureReason = "lease expired";
        failedStep.leaseExpiresAt.reset();
        failedStep.completedAt = std::chrono::system_clock::now();
        stepExecutionStore_.update(failedStep);

        WorkflowExecution updatedExecution = *execution;

        if (updatedExecution.currentStepAttempt < maxRetries)
        {
            ++updatedExecution.currentStepAttempt;
            executionStore_.update(updatedExecution);
            stepExecutionStore_.save(makeStepExecution(
                updatedExecution, expiredStep.stepName, updatedExecution.currentStepAttempt
            ));
            ++sweepResult.retriedCount;
        }
        else
        {
            updatedExecution.status = WorkflowExecutionStatus::Failed;
            updatedExecution.failureReason = "lease expired";
            updatedExecution.completedAt = std::chrono::system_clock::now();
            executionStore_.update(updatedExecution);
            ++sweepResult.failedCount;
        }
    }

    return sweepResult;
}

std::optional<WorkflowExecution>
WorkflowOrchestrator::getWorkflowExecution(const std::string& workflowExecutionId) const
{
    validateExecutionId(workflowExecutionId);
    return executionStore_.find(workflowExecutionId);
}

std::vector<WorkflowDefinitionKey> WorkflowOrchestrator::listWorkflowDefinitions() const
{
    return definitionStore_.list();
}

WorkflowDefinitionStore& WorkflowOrchestrator::workflowDefinitionStore()
{
    return definitionStore_;
}

const WorkflowDefinitionStore& WorkflowOrchestrator::workflowDefinitionStore() const
{
    return definitionStore_;
}

WorkflowExecutionStore& WorkflowOrchestrator::workflowExecutionStore()
{
    return executionStore_;
}

const WorkflowExecutionStore& WorkflowOrchestrator::workflowExecutionStore() const
{
    return executionStore_;
}

WorkflowStepExecutionStore& WorkflowOrchestrator::workflowStepExecutionStore()
{
    return stepExecutionStore_;
}

const WorkflowStepExecutionStore& WorkflowOrchestrator::workflowStepExecutionStore() const
{
    return stepExecutionStore_;
}

} // namespace workflow
