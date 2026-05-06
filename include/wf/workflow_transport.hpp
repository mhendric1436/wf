#pragma once

#include "wf/workflow_service.hpp"

namespace workflow
{

class IWorkflowTransport
{
  public:
    virtual ~IWorkflowTransport() = default;

    virtual ValidateWorkflowDefinitionResponse
    validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request) = 0;

    virtual RegisterWorkflowDefinitionResponse
    registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request) = 0;

    virtual ListWorkflowDefinitionsResponse
    listWorkflowDefinitions(const ListWorkflowDefinitionsRequest& request) = 0;

    virtual StartWorkflowExecutionResponse
    startWorkflowExecution(const StartWorkflowExecutionRequest& request) = 0;

    virtual GetWorkflowExecutionResponse
    getWorkflowExecution(const GetWorkflowExecutionRequest& request) = 0;

    virtual CancelWorkflowResponse cancelWorkflow(const CancelWorkflowRequest& request) = 0;

    virtual PollAndClaimWorkflowStepsResponse
    pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request) = 0;

    virtual KeepAliveWorkflowStepResponse
    keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request) = 0;

    virtual CompleteWorkflowStepResponse
    completeWorkflowStep(const CompleteWorkflowStepRequest& request) = 0;

    virtual FailWorkflowStepResponse failWorkflowStep(const FailWorkflowStepRequest& request) = 0;
};

} // namespace workflow
