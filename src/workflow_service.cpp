#include "wf/workflow_service.hpp"

namespace workflow {

WorkflowService::WorkflowService(WorkflowOrchestrator& orchestrator)
    : orchestrator_(orchestrator) {}

ValidateWorkflowDefinitionResponse WorkflowService::validateWorkflowDefinition(
    const ValidateWorkflowDefinitionRequest& request
) const {
    return ValidateWorkflowDefinitionResponse{
        .validation = validateWorkflowJson(request.definitionJson),
    };
}

RegisterWorkflowDefinitionResponse WorkflowService::registerWorkflowDefinition(
    const RegisterWorkflowDefinitionRequest& request
) {
    WorkflowDefinition definition = parseWorkflowDefinition(request.definitionJson);

    orchestrator_.workflowDefinitionStore().save(definition);

    return RegisterWorkflowDefinitionResponse{
        .definition = definition,
    };
}

StartWorkflowExecutionResponse WorkflowService::startWorkflowExecution(
    const StartWorkflowExecutionRequest& request
) {
    return StartWorkflowExecutionResponse{
        .execution = orchestrator_.startWorkflow(
            request.workflowName,
            request.workflowVersion,
            request.input
        ),
    };
}

CompleteWorkflowStepResponse WorkflowService::completeWorkflowStep(
    const CompleteWorkflowStepRequest& request
) {
    return CompleteWorkflowStepResponse{
        .execution = orchestrator_.completeStep(
            request.workflowExecutionId,
            request.stepName,
            request.stepOutput
        ),
    };
}

FailWorkflowStepResponse WorkflowService::failWorkflowStep(
    const FailWorkflowStepRequest& request
) {
    return FailWorkflowStepResponse{
        .execution = orchestrator_.failStep(
            request.workflowExecutionId,
            request.stepName,
            request.reason
        ),
    };
}

} // namespace workflow
