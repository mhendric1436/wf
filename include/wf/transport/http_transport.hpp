#pragma once

#include "wf/workflow_transport.hpp"

#include <memory>
#include <string>

namespace workflow::transport
{

class HttpTransport : public IWorkflowTransport
{
  public:
    HttpTransport(
        const std::string& host,
        int port
    );
    ~HttpTransport();

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace workflow::transport
