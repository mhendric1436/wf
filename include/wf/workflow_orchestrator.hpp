#pragma once

#include "mt/json.hpp"
#include "wf/store/workflow_definition_store.hpp"
#include "wf/store/workflow_execution_store.hpp"
#include "wf/store/workflow_step_execution_store.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_step_execution.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <mutex>
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
        WorkflowDefinitionStore& definitionStore,
        WorkflowExecutionStore& executionStore,
        WorkflowStepExecutionStore& stepExecutionStore,
        WorkflowLogic& workflowLogic
    );

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

    std::vector<WorkflowDefinitionKey> listWorkflowDefinitions() const;

    WorkflowDefinitionStore& workflowDefinitionStore();
    const WorkflowDefinitionStore& workflowDefinitionStore() const;

    WorkflowExecutionStore& workflowExecutionStore();
    const WorkflowExecutionStore& workflowExecutionStore() const;

    WorkflowStepExecutionStore& workflowStepExecutionStore();
    const WorkflowStepExecutionStore& workflowStepExecutionStore() const;

  private:
    static constexpr std::size_t STRIPE_COUNT = 64;

    std::mutex& stripeFor(const std::string& executionId) const;

    mutable std::array<std::mutex, STRIPE_COUNT> executionStripes_;
    WorkflowDefinitionStore& definitionStore_;
    WorkflowExecutionStore& executionStore_;
    WorkflowStepExecutionStore& stepExecutionStore_;
    WorkflowLogic& workflowLogic_;
};

} // namespace workflow
