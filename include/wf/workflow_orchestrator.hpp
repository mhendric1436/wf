#pragma once

#include "mt/database.hpp"
#include "mt/json.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_step_execution.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace workflow
{

struct SweepResult
{
    std::size_t retriedCount = 0;
    std::size_t failedCount = 0;
};

class WorkflowOrchestrator
{
  public:
    WorkflowOrchestrator(
        mt::Database& database,
        WorkflowLogic& workflowLogic
    );

    ~WorkflowOrchestrator();

    WorkflowOrchestrator(const WorkflowOrchestrator&) = delete;
    WorkflowOrchestrator& operator=(const WorkflowOrchestrator&) = delete;

    WorkflowExecution startWorkflow(
        const std::string& workflowName,
        int workflowVersion,
        const mt::Json& input
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
        const mt::Json& stepOutput
    );

    WorkflowExecution failStep(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        const std::string& workerId,
        const std::string& reason
    );

    WorkflowExecution cancelWorkflow(const std::string& workflowExecutionId);

    SweepResult sweepExpiredLeases();

    std::optional<WorkflowExecution>
    getWorkflowExecution(const std::string& workflowExecutionId) const;

    WorkflowExecution putWorkflowExecution(const WorkflowExecution& execution);

    std::optional<WorkflowStepExecution> getWorkflowStepExecution(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    ) const;

    WorkflowStepExecution putWorkflowStepExecution(const WorkflowStepExecution& stepExecution);

    std::vector<WorkflowDefinitionKey> listWorkflowDefinitions() const;

    std::optional<WorkflowDefinition> getWorkflowDefinition(
        const std::string& workflowName,
        int workflowVersion
    ) const;

    WorkflowDefinition registerWorkflowDefinition(const WorkflowDefinition& definition);

  private:
    struct Tables;

    std::unique_ptr<Tables> tables_;
    WorkflowLogic& workflowLogic_;
};

} // namespace workflow
