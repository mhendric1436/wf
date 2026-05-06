#pragma once

#include "wf/workflow_service.hpp"
#include "wf/workflow_transport.hpp"

namespace workflow::transport
{

class InProcessTransport : public IWorkflowTransport
{
  public:
    explicit InProcessTransport(WorkflowService& service);

    ValidateWorkflowDefinitionResponse
    validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request) override;

    RegisterWorkflowDefinitionResponse
    registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request) override;

    ListWorkflowDefinitionsResponse
    listWorkflowDefinitions(const ListWorkflowDefinitionsRequest& request) override;

    StartWorkflowExecutionResponse
    startWorkflowExecution(const StartWorkflowExecutionRequest& request) override;

    GetWorkflowExecutionResponse
    getWorkflowExecution(const GetWorkflowExecutionRequest& request) override;

    CancelWorkflowResponse cancelWorkflow(const CancelWorkflowRequest& request) override;

    PollAndClaimWorkflowStepsResponse
    pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request) override;

    KeepAliveWorkflowStepResponse
    keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request) override;

    CompleteWorkflowStepResponse
    completeWorkflowStep(const CompleteWorkflowStepRequest& request) override;

    FailWorkflowStepResponse failWorkflowStep(const FailWorkflowStepRequest& request) override;

  private:
    WorkflowService& service_;
};

} // namespace workflow::transport
