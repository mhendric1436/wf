#pragma once

#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace workflow
{

class WorkflowStepExecutionStore
{
  public:
    virtual ~WorkflowStepExecutionStore() = default;

    virtual void save(const WorkflowStepExecution& stepExecution) = 0;

    virtual std::optional<WorkflowStepExecution> find(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    ) const = 0;

    virtual std::vector<WorkflowStepExecution> pollAndClaim(
        const std::string& workflowName,
        int workflowVersion,
        const std::string& workerId,
        std::size_t maxResults,
        const std::map<
            std::string,
            std::chrono::seconds>& leaseDurationsByStepName
    ) = 0;

    virtual WorkflowStepExecution keepAlive(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt,
        const std::string& workerId,
        std::chrono::seconds leaseDuration
    ) = 0;

    virtual void update(const WorkflowStepExecution& stepExecution) = 0;

    virtual void cancelByExecution(const std::string& workflowExecutionId) = 0;

    virtual void remove(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    ) = 0;
};

} // namespace workflow
