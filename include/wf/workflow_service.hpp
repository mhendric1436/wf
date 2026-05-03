#pragma once

#include "wf/json.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_parser.hpp"

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
    int workflowVersion;
    json::Value input = json::Value::object();
};

struct StartWorkflowExecutionResponse
{
    WorkflowExecution execution;
};

struct CompleteWorkflowStepRequest
{
    std::string workflowExecutionId;
    std::string stepName;
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
    std::string reason;
};

struct FailWorkflowStepResponse
{
    WorkflowExecution execution;
};

class WorkflowService
{
  public:
    explicit WorkflowService(
        WorkflowDefinitionStore& definitionStore,
        WorkflowOrchestrator& orchestrator
    );

    ValidateWorkflowDefinitionResponse
    validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request) const;

    RegisterWorkflowDefinitionResponse
    registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request);

    StartWorkflowExecutionResponse
    startWorkflowExecution(const StartWorkflowExecutionRequest& request);

    CompleteWorkflowStepResponse completeWorkflowStep(const CompleteWorkflowStepRequest& request);

    FailWorkflowStepResponse failWorkflowStep(const FailWorkflowStepRequest& request);

  private:
    WorkflowDefinitionStore& definitionStore_;
    WorkflowOrchestrator& orchestrator_;
};

} // namespace workflow
