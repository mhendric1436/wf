#pragma once

#include "wf/json.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_parser.hpp"

#include <optional>
#include <string>
#include <vector>

namespace workflow {

// -----------------------------------------------------------------------------
// Workflow Definition API
// -----------------------------------------------------------------------------

struct RegisterWorkflowDefinitionRequest {
    json::Value definitionJson;
};

struct RegisterWorkflowDefinitionResponse {
    WorkflowDefinition definition;
};

struct ValidateWorkflowDefinitionRequest {
    json::Value definitionJson;
};

struct ValidateWorkflowDefinitionResponse {
    ValidationResult validation;
};

// -----------------------------------------------------------------------------
// StartWorkflowExecution API
//
// Intended caller:
//   Frontend API tier
// -----------------------------------------------------------------------------

struct StartWorkflowExecutionRequest {
    std::string workflowName;
    int workflowVersion = 0;
    json::Value input = json::Value::object();
};

struct StartWorkflowExecutionResponse {
    WorkflowExecution execution;
};

// -----------------------------------------------------------------------------
// StepExecution API
//
// Intended caller:
//   Backend worker tier
// -----------------------------------------------------------------------------

struct WorkflowStepExecution {
    std::string workflowExecutionId;
    std::string workflowName;
    int workflowVersion = 0;

    std::string stepName;
    int attempt = 0;

    StepExecutionStatus status = StepExecutionStatus::Pending;

    std::optional<std::string> workerId;
    std::optional<std::string> failureReason;

    json::Value input = json::Value::object();
    json::Value state = json::Value::object();
    json::Value output = json::Value::object();
};

struct PollAndClaimWorkflowStepsRequest {
    std::string workflowName;
    int workflowVersion = 0;
    std::string workerId;
    std::size_t maxResults = 1;
};

struct PollAndClaimWorkflowStepsResponse {
    std::vector<WorkflowStepExecution> steps;
};

struct CompleteWorkflowStepRequest {
    std::string workflowExecutionId;
    std::string stepName;
    std::string workerId;
    json::Value stepOutput = json::Value::object();
};

struct CompleteWorkflowStepResponse {
    WorkflowExecution execution;
};

struct FailWorkflowStepRequest {
    std::string workflowExecutionId;
    std::string stepName;
    std::string workerId;
    std::string reason;
};

struct FailWorkflowStepResponse {
    WorkflowExecution execution;
};

// -----------------------------------------------------------------------------
// WorkflowService
// -----------------------------------------------------------------------------

class WorkflowService {
  public:
    explicit WorkflowService(WorkflowOrchestrator& orchestrator);

    ValidateWorkflowDefinitionResponse validateWorkflowDefinition(
        const ValidateWorkflowDefinitionRequest& request
    ) const;

    RegisterWorkflowDefinitionResponse registerWorkflowDefinition(
        const RegisterWorkflowDefinitionRequest& request
    );

    // Frontend API tier
    StartWorkflowExecutionResponse startWorkflowExecution(
        const StartWorkflowExecutionRequest& request
    );

    // Backend worker tier
    PollAndClaimWorkflowStepsResponse pollAndClaimWorkflowSteps(
        const PollAndClaimWorkflowStepsRequest& request
    );

    CompleteWorkflowStepResponse completeWorkflowStep(
        const CompleteWorkflowStepRequest& request
    );

    FailWorkflowStepResponse failWorkflowStep(
        const FailWorkflowStepRequest& request
    );

  private:
    WorkflowOrchestrator& orchestrator_;
};

} // namespace workflow
