#pragma once

#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/store/workflow_step_execution_store.hpp"

namespace workflow::backend::sqlite
{

class SQLiteWorkflowStepExecutionStore final : public WorkflowStepExecutionStore
{
  public:
    explicit SQLiteWorkflowStepExecutionStore(SQLiteDatabase& db);

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

    void cancelByExecution(const std::string& workflowExecutionId) override;

    std::vector<WorkflowStepExecution> findExpiredRunning() const override;

    void remove(
        const std::string& workflowExecutionId,
        const std::string& stepName,
        int attempt
    ) override;

  private:
    SQLiteDatabase& db_;
};

} // namespace workflow::backend::sqlite
