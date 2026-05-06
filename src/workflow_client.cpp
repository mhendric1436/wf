#include "wf/workflow_client.hpp"

namespace workflow
{

WorkflowClient::WorkflowClient(std::unique_ptr<IWorkflowTransport> transport)
    : transport_(std::move(transport))
{
}

ValidateWorkflowDefinitionResponse
WorkflowClient::validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request)
{
    return transport_->validateWorkflowDefinition(request);
}

RegisterWorkflowDefinitionResponse
WorkflowClient::registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request)
{
    return transport_->registerWorkflowDefinition(request);
}

ListWorkflowDefinitionsResponse
WorkflowClient::listWorkflowDefinitions(const ListWorkflowDefinitionsRequest& request)
{
    return transport_->listWorkflowDefinitions(request);
}

StartWorkflowExecutionResponse
WorkflowClient::startWorkflowExecution(const StartWorkflowExecutionRequest& request)
{
    return transport_->startWorkflowExecution(request);
}

GetWorkflowExecutionResponse
WorkflowClient::getWorkflowExecution(const GetWorkflowExecutionRequest& request)
{
    return transport_->getWorkflowExecution(request);
}

CancelWorkflowResponse WorkflowClient::cancelWorkflow(const CancelWorkflowRequest& request)
{
    return transport_->cancelWorkflow(request);
}

PollAndClaimWorkflowStepsResponse
WorkflowClient::pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request)
{
    return transport_->pollAndClaimWorkflowSteps(request);
}

KeepAliveWorkflowStepResponse
WorkflowClient::keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request)
{
    return transport_->keepAliveWorkflowStep(request);
}

CompleteWorkflowStepResponse
WorkflowClient::completeWorkflowStep(const CompleteWorkflowStepRequest& request)
{
    return transport_->completeWorkflowStep(request);
}

FailWorkflowStepResponse WorkflowClient::failWorkflowStep(const FailWorkflowStepRequest& request)
{
    return transport_->failWorkflowStep(request);
}

} // namespace workflow
