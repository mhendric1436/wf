#pragma once

#include "wf/json.hpp"
#include "wf/store/workflow_definition_store.hpp"
#include "wf/store/workflow_execution_store.hpp"
#include "wf/store/workflow_step_execution_store.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_step_execution.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace workflow
{

class WorkflowOrchestrator
{
  public:
    WorkflowOrchestrator(
        WorkflowDefinitionStore& definitionStore,
        WorkflowExecutionStore& executionStore,
        WorkflowStepExecutionStore& stepExecutionStore,
        WorkflowLogic& workflowLogic
    );

    WorkflowExecution startWorkflow(
        const std::string& workflowName,
        int workflowVersion,
        const json::Value& input
    );

    std::vector<WorkflowStepExecution> pollAndClaimWorkflowSteps(
        const std::string& workflowName,
        int workflowVersion,
        const std::string& workerId,
        std::size_t maxResults
    );

    WorkflowStepExecution keepAliveStep(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        const std::string& workerId
    );

    WorkflowExecution completeStep(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        const std::string& workerId,
        const json::Value& stepOutput
    );

    WorkflowExecution failStep(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        const std::string& workerId,
        const std::string& reason
    );

    std::optional<WorkflowExecution>
    getWorkflowExecution(const std::string& workflowExecutionId) const;

    std::vector<WorkflowDefinitionKey> listWorkflowDefinitions() const;

    WorkflowDefinitionStore& workflowDefinitionStore();
    const WorkflowDefinitionStore& workflowDefinitionStore() const;

    WorkflowExecutionStore& workflowExecutionStore();
    const WorkflowExecutionStore& workflowExecutionStore() const;

    WorkflowStepExecutionStore& workflowStepExecutionStore();
    const WorkflowStepExecutionStore& workflowStepExecutionStore() const;

  private:
    WorkflowDefinitionStore& definitionStore_;
    WorkflowExecutionStore& executionStore_;
    WorkflowStepExecutionStore& stepExecutionStore_;
    WorkflowLogic& workflowLogic_;
};

} // namespace workflow
