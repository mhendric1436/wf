#pragma once

#include "wf/json.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_parser.hpp"
#include "wf/workflow_step_execution.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace workflow
{

struct RegisterWorkflowDefinitionRequest
{
    json::Value definitionJson;
};

struct RegisterWorkflowDefinitionResponse
{
    WorkflowDefinition definition;
};

struct ValidateWorkflowDefinitionRequest
{
    json::Value definitionJson;
};

struct ValidateWorkflowDefinitionResponse
{
    ValidationResult validation;
};

struct StartWorkflowExecutionRequest
{
    std::string workflowName;
    int workflowVersion = 0;
    json::Value input = json::Value::object();
};

struct StartWorkflowExecutionResponse
{
    WorkflowExecution execution;
};

struct PollAndClaimWorkflowStepsRequest
{
    std::string workflowName;
    int workflowVersion = 0;
    std::string workerId;
    std::size_t maxResults = 1;
};

struct PollAndClaimWorkflowStepsResponse
{
    std::vector<WorkflowStepExecution> steps;
};

struct KeepAliveWorkflowStepRequest
{
    std::string workflowExecutionId;
    std::string stepName;
    std::string workerId;
};

struct KeepAliveWorkflowStepResponse
{
    WorkflowStepExecution step;
};

struct CompleteWorkflowStepRequest
{
    std::string workflowExecutionId;
    std::string stepName;
    std::string workerId;
    json::Value stepOutput = json::Value::object();
};

struct CompleteWorkflowStepResponse
{
    WorkflowExecution execution;
};

struct FailWorkflowStepRequest
{
    std::string workflowExecutionId;
    std::string stepName;
    std::string workerId;
    std::string reason;
};

struct FailWorkflowStepResponse
{
    WorkflowExecution execution;
};

class WorkflowService
{
  public:
    explicit WorkflowService(WorkflowOrchestrator& orchestrator);

    ValidateWorkflowDefinitionResponse
    validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request) const;

    RegisterWorkflowDefinitionResponse
    registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request);

    StartWorkflowExecutionResponse
    startWorkflowExecution(const StartWorkflowExecutionRequest& request);

    PollAndClaimWorkflowStepsResponse
    pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request);

    KeepAliveWorkflowStepResponse
    keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request);

    CompleteWorkflowStepResponse completeWorkflowStep(const CompleteWorkflowStepRequest& request);

    FailWorkflowStepResponse failWorkflowStep(const FailWorkflowStepRequest& request);

  private:
    WorkflowOrchestrator& orchestrator_;
};

} // namespace workflow
