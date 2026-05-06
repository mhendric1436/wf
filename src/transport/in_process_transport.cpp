#include "wf/transport/in_process_transport.hpp"

namespace workflow::transport
{

InProcessTransport::InProcessTransport(WorkflowService& service)
    : service_(service)
{
}

ValidateWorkflowDefinitionResponse
InProcessTransport::validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request)
{
    return service_.validateWorkflowDefinition(request);
}

RegisterWorkflowDefinitionResponse
InProcessTransport::registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request)
{
    return service_.registerWorkflowDefinition(request);
}

ListWorkflowDefinitionsResponse
InProcessTransport::listWorkflowDefinitions(const ListWorkflowDefinitionsRequest& request)
{
    return service_.listWorkflowDefinitions(request);
}

StartWorkflowExecutionResponse
InProcessTransport::startWorkflowExecution(const StartWorkflowExecutionRequest& request)
{
    return service_.startWorkflowExecution(request);
}

GetWorkflowExecutionResponse
InProcessTransport::getWorkflowExecution(const GetWorkflowExecutionRequest& request)
{
    return service_.getWorkflowExecution(request);
}

CancelWorkflowResponse InProcessTransport::cancelWorkflow(const CancelWorkflowRequest& request)
{
    return service_.cancelWorkflow(request);
}

PollAndClaimWorkflowStepsResponse
InProcessTransport::pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request)
{
    return service_.pollAndClaimWorkflowSteps(request);
}

KeepAliveWorkflowStepResponse
InProcessTransport::keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request)
{
    return service_.keepAliveWorkflowStep(request);
}

CompleteWorkflowStepResponse
InProcessTransport::completeWorkflowStep(const CompleteWorkflowStepRequest& request)
{
    return service_.completeWorkflowStep(request);
}

FailWorkflowStepResponse
InProcessTransport::failWorkflowStep(const FailWorkflowStepRequest& request)
{
    return service_.failWorkflowStep(request);
}

} // namespace workflow::transport
