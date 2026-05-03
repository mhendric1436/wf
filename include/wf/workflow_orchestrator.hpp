#pragma once

#include "wf/workflow_definition_store.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_execution_store.hpp"
#include "wf/workflow_logic.hpp"

#include <string>

namespace workflow
{

class WorkflowOrchestrator
{
  public:
    WorkflowOrchestrator(
        WorkflowDefinitionStore& definitionStore,
        WorkflowExecutionStore& executionStore,
        WorkflowLogic& workflowLogic
    );

    WorkflowExecution startWorkflow(
        const std::string& workflowName,
        int workflowVersion,
        const json::Value& input
    );

    WorkflowExecution completeStep(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        const json::Value& stepOutput
    );

    WorkflowExecution failStep(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        const std::string& reason
    );

  private:
    WorkflowDefinitionStore& definitionStore_;
    WorkflowExecutionStore& executionStore_;
    WorkflowLogic& workflowLogic_;
};

} // namespace workflow
