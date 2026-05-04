#pragma once

#include "wf/store/workflow_step_execution_store.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace workflow::backend::memory
{

class InMemoryWorkflowStepExecutionStore final : public workflow::WorkflowStepExecutionStore
{
  public:
    void save(const WorkflowStepExecution& stepExecution) override;

    std::optional<WorkflowStepExecution> find(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    ) const override;

    std::vector<WorkflowStepExecution> pollAndClaim(
        const std::string& workflowName,
        int workflowVersion,
        const std::string& workerId,
        std::size_t maxResults
    ) override;

    void update(const WorkflowStepExecution& stepExecution) override;

    void remove(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    ) override;

    void clear();

    std::size_t size() const;

  private:
    using Key = std::tuple<std::string, std::string, int>;

    static Key makeKey(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    );

    static Key makeKey(const WorkflowStepExecution& stepExecution);

    static void validateIdentity(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    );

    static void validateStepExecution(const WorkflowStepExecution& stepExecution);

    static void validatePollAndClaimRequest(
        const std::string& workflowName,
        int workflowVersion,
        const std::string& workerId,
        std::size_t maxResults
    );

    mutable std::mutex mutex_;
    std::map<Key, WorkflowStepExecution> stepExecutions_;
};

} // namespace workflow::backend::memory
