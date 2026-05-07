#pragma once

#include "mt/json.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_json.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_step_execution.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace workflow
{

struct RegisterWorkflowDefinitionRequest
{
    mt::Json definitionJson;
};

struct RegisterWorkflowDefinitionResponse
{
    WorkflowDefinition definition;
};

struct ValidateWorkflowDefinitionRequest
{
    mt::Json definitionJson;
};

struct ValidateWorkflowDefinitionResponse
{
    ValidationResult validation;
};

struct StartWorkflowExecutionRequest
{
    std::string workflowName;
    int workflowVersion = 0;
    mt::Json input = mt::Json(mt::Json::Object{});
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
    mt::Json stepOutput = mt::Json(mt::Json::Object{});
    std::optional<std::chrono::seconds> nextStepDelay;
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

struct CancelWorkflowRequest
{
    std::string workflowExecutionId;
};

struct CancelWorkflowResponse
{
    WorkflowExecution execution;
};

struct GetWorkflowExecutionRequest
{
    std::string workflowExecutionId;
};

struct GetWorkflowExecutionResponse
{
    std::optional<WorkflowExecution> execution;
};

struct ListWorkflowDefinitionsRequest
{
};

struct ListWorkflowDefinitionsResponse
{
    std::vector<WorkflowDefinitionKey> definitions;
};

class WorkflowService
{
  public:
    explicit WorkflowService(
        WorkflowOrchestrator& orchestrator,
        std::chrono::seconds sweepInterval = std::chrono::seconds{30}
    );

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

    ~WorkflowService();

    CancelWorkflowResponse cancelWorkflow(const CancelWorkflowRequest& request);

    GetWorkflowExecutionResponse
    getWorkflowExecution(const GetWorkflowExecutionRequest& request) const;

    ListWorkflowDefinitionsResponse
    listWorkflowDefinitions(const ListWorkflowDefinitionsRequest& request) const;

  private:
    void runSweepLoop();

    WorkflowOrchestrator& orchestrator_;
    std::chrono::seconds sweepInterval_;
    std::atomic<bool> stopping_{false};
    std::mutex sweepMutex_;
    std::condition_variable sweepCv_;
    std::thread sweepThread_;
};

} // namespace workflow
