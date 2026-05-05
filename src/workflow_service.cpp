#include "wf/workflow_service.hpp"

namespace workflow
{

WorkflowService::WorkflowService(WorkflowOrchestrator& orchestrator)
    : orchestrator_(orchestrator)
{
}

ValidateWorkflowDefinitionResponse
WorkflowService::validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request) const
{
    return ValidateWorkflowDefinitionResponse{
        .validation = validateWorkflowJson(request.definitionJson),
    };
}

RegisterWorkflowDefinitionResponse
WorkflowService::registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request)
{
    WorkflowDefinition definition = parseWorkflowDefinition(request.definitionJson);

    orchestrator_.workflowDefinitionStore().save(definition);

    return RegisterWorkflowDefinitionResponse{
        .definition = definition,
    };
}

StartWorkflowExecutionResponse
WorkflowService::startWorkflowExecution(const StartWorkflowExecutionRequest& request)
{
    return StartWorkflowExecutionResponse{
        .execution = orchestrator_.startWorkflow(
            request.workflowName, request.workflowVersion, request.input
        ),
    };
}

PollAndClaimWorkflowStepsResponse
WorkflowService::pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request)
{
    return PollAndClaimWorkflowStepsResponse{
        .steps = orchestrator_.pollAndClaimWorkflowSteps(
            request.workflowName, request.workflowVersion, request.workerId, request.maxResults
        ),
    };
}

KeepAliveWorkflowStepResponse
WorkflowService::keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request)
{
    return KeepAliveWorkflowStepResponse{
        .step = orchestrator_.keepAliveStep(
            request.workflowExecutionId, request.stepName, request.workerId
        ),
    };
}

CompleteWorkflowStepResponse
WorkflowService::completeWorkflowStep(const CompleteWorkflowStepRequest& request)
{
    return CompleteWorkflowStepResponse{
        .execution = orchestrator_.completeStep(
            request.workflowExecutionId, request.stepName, request.workerId, request.stepOutput
        ),
    };
}

FailWorkflowStepResponse WorkflowService::failWorkflowStep(const FailWorkflowStepRequest& request)
{
    return FailWorkflowStepResponse{
        .execution = orchestrator_.failStep(
            request.workflowExecutionId, request.stepName, request.workerId, request.reason
        ),
    };
}

CancelWorkflowResponse WorkflowService::cancelWorkflow(const CancelWorkflowRequest& request)
{
    return CancelWorkflowResponse{
        .execution = orchestrator_.cancelWorkflow(request.workflowExecutionId),
    };
}

GetWorkflowExecutionResponse
WorkflowService::getWorkflowExecution(const GetWorkflowExecutionRequest& request) const
{
    return GetWorkflowExecutionResponse{
        .execution = orchestrator_.getWorkflowExecution(request.workflowExecutionId),
    };
}

ListWorkflowDefinitionsResponse
WorkflowService::listWorkflowDefinitions(const ListWorkflowDefinitionsRequest&) const
{
    return ListWorkflowDefinitionsResponse{
        .definitions = orchestrator_.listWorkflowDefinitions(),
    };
}

} // namespace workflow
