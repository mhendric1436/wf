#include "wf/workflow_orchestrator.hpp"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <stdexcept>
#include <string>

namespace workflow {
namespace {

std::atomic<unsigned long long> NEXT_WORKFLOW_EXECUTION_ID{1};

std::string makeWorkflowExecutionId() {
    const auto id = NEXT_WORKFLOW_EXECUTION_ID.fetch_add(1);
    return "wfexec-" + std::to_string(id);
}

const WorkflowStep* findStep(const WorkflowDefinition& definition, const std::string& stepName) {
    const auto it = std::find_if(
        definition.steps.begin(),
        definition.steps.end(),
        [&stepName](const WorkflowStep& step) { return step.name == stepName; }
    );

    if (it == definition.steps.end()) {
        return nullptr;
    }

    return &(*it);
}

WorkflowDefinition requireDefinition(
    WorkflowDefinitionStore& definitionStore,
    const std::string& workflowName,
    int workflowVersion
) {
    auto definition = definitionStore.find(workflowName, workflowVersion);

    if (!definition.has_value()) {
        std::ostringstream message;
        message << "Workflow definition not found: " << workflowName << " version "
                << workflowVersion;
        throw std::invalid_argument(message.str());
    }

    return definition.value();
}

WorkflowExecution requireExecution(
    WorkflowExecutionStore& executionStore,
    const std::string& workflowExecutionId
) {
    auto execution = executionStore.find(workflowExecutionId);

    if (!execution.has_value()) {
        throw std::invalid_argument("Workflow execution not found: " + workflowExecutionId);
    }

    return execution.value();
}

void requireRunning(const WorkflowExecution& execution) {
    if (execution.status != WorkflowExecutionStatus::Running) {
        throw std::logic_error("Workflow execution is not running: " + execution.workflowExecutionId);
    }
}

void requireCurrentStep(const WorkflowExecution& execution, const std::string& stepName) {
    if (execution.currentStepName != stepName) {
        std::ostringstream message;
        message << "Step '" << stepName << "' is not the current step. Current step is '"
                << execution.currentStepName << "'";
        throw std::logic_error(message.str());
    }
}

} // namespace

WorkflowOrchestrator::WorkflowOrchestrator(
    WorkflowDefinitionStore& definitionStore,
    WorkflowExecutionStore& executionStore,
    WorkflowLogic& workflowLogic
)
    : definitionStore_(definitionStore),
      executionStore_(executionStore),
      workflowLogic_(workflowLogic) {}

WorkflowExecution WorkflowOrchestrator::startWorkflow(
    const std::string& workflowName,
    int workflowVersion,
    const json::Value& input
) {
    const auto definition = requireDefinition(definitionStore_, workflowName, workflowVersion);

    const auto* startStep = findStep(definition, definition.startWorkflowStepName);
    if (startStep == nullptr) {
        throw std::logic_error(
            "Workflow definition start step does not exist: " + definition.startWorkflowStepName
        );
    }

    WorkflowExecution execution;
    execution.workflowExecutionId = makeWorkflowExecutionId();
    execution.workflowName = workflowName;
    execution.workflowVersion = workflowVersion;
    execution.status = WorkflowExecutionStatus::Running;
    execution.currentStepName = definition.startWorkflowStepName;
    execution.input = input;
    execution.state = json::Value::object();
    execution.currentStepAttempt = 0;

    executionStore_.save(execution);

    return execution;
}

WorkflowExecution WorkflowOrchestrator::completeStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const json::Value& stepOutput
) {
    auto execution = requireExecution(executionStore_, workflowExecutionId);

    requireRunning(execution);
    requireCurrentStep(execution, stepName);

    const auto definition =
        requireDefinition(definitionStore_, execution.workflowName, execution.workflowVersion);

    const auto* currentStep = findStep(definition, stepName);
    if (currentStep == nullptr) {
        throw std::logic_error("Current step does not exist in workflow definition: " + stepName);
    }

    StepCompletionContext context;
    context.workflowName = execution.workflowName;
    context.workflowVersion = execution.workflowVersion;
    context.workflowExecutionId = execution.workflowExecutionId;
    context.completedStepName = stepName;
    context.input = execution.input;
    context.state = execution.state;
    context.stepOutput = stepOutput;

    const auto decision = workflowLogic_.decideNextStep(context);

    execution.state = decision.updatedState;
    execution.currentStepAttempt = 0;

    if (decision.workflowComplete) {
        execution.status = WorkflowExecutionStatus::Completed;
        execution.currentStepName.clear();
        executionStore_.update(execution);
        return execution;
    }

    if (!decision.nextStepName.has_value() || decision.nextStepName.value().empty()) {
        throw std::logic_error(
            "Workflow logic must either complete the workflow or return a next step"
        );
    }

    const auto nextStepName = decision.nextStepName.value();
    const auto* nextStep = findStep(definition, nextStepName);

    if (nextStep == nullptr) {
        throw std::logic_error(
            "Workflow logic returned a step that does not exist in the definition: " +
            nextStepName
        );
    }

    execution.currentStepName = nextStepName;

    executionStore_.update(execution);

    return execution;
}

WorkflowExecution WorkflowOrchestrator::failStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& reason
) {
    auto execution = requireExecution(executionStore_, workflowExecutionId);

    requireRunning(execution);
    requireCurrentStep(execution, stepName);

    const auto definition =
        requireDefinition(definitionStore_, execution.workflowName, execution.workflowVersion);

    const auto* currentStep = findStep(definition, stepName);
    if (currentStep == nullptr) {
        throw std::logic_error("Current step does not exist in workflow definition: " + stepName);
    }

    const int maxRetries = currentStep->maxRetries.value_or(0);

    ++execution.currentStepAttempt;

    if (execution.currentStepAttempt <= maxRetries) {
        execution.failureReason = reason;
        executionStore_.update(execution);
        return execution;
    }

    execution.status = WorkflowExecutionStatus::Failed;
    execution.failureReason = reason;

    executionStore_.update(execution);

    return execution;
}

} // namespace workflow
