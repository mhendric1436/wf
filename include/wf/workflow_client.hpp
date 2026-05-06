#pragma once

#include "wf/workflow_transport.hpp"

#include <memory>

namespace workflow
{

class WorkflowClient
{
  public:
    explicit WorkflowClient(std::unique_ptr<IWorkflowTransport> transport);

    ValidateWorkflowDefinitionResponse
    validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request);

    RegisterWorkflowDefinitionResponse
    registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request);

    ListWorkflowDefinitionsResponse
    listWorkflowDefinitions(const ListWorkflowDefinitionsRequest& request);

    StartWorkflowExecutionResponse
    startWorkflowExecution(const StartWorkflowExecutionRequest& request);

    GetWorkflowExecutionResponse getWorkflowExecution(const GetWorkflowExecutionRequest& request);

    CancelWorkflowResponse cancelWorkflow(const CancelWorkflowRequest& request);

    PollAndClaimWorkflowStepsResponse
    pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request);

    KeepAliveWorkflowStepResponse
    keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request);

    CompleteWorkflowStepResponse completeWorkflowStep(const CompleteWorkflowStepRequest& request);

    FailWorkflowStepResponse failWorkflowStep(const FailWorkflowStepRequest& request);

  private:
    std::unique_ptr<IWorkflowTransport> transport_;
};

} // namespace workflow
