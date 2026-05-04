#pragma once

#include "wf/store/workflow_step_execution_store.hpp"

#include <chrono>
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
        std::size_t maxResults,
        const std::map<
            std::string,
            std::chrono::seconds>& leaseDurationsByStepName
    ) override;

    WorkflowStepExecution keepAlive(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt,
        const std::string& workerId,
        std::chrono::seconds leaseDuration
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

    static void validateWorkerId(const std::string& workerId);

    static void validateLeaseDuration(std::chrono::seconds leaseDuration);

    static void validatePollAndClaimRequest(
        const std::string& workflowName,
        int workflowVersion,
        const std::string& workerId,
        std::size_t maxResults,
        const std::map<
            std::string,
            std::chrono::seconds>& leaseDurationsByStepName
    );

    static bool isClaimable(
        const WorkflowStepExecution& stepExecution,
        std::chrono::system_clock::time_point now
    );

    mutable std::mutex mutex_;
    std::map<Key, WorkflowStepExecution> stepExecutions_;
};

} // namespace workflow::backend::memory
