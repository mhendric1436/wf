#pragma once

#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/store/workflow_execution_store.hpp"

namespace workflow::backend::sqlite
{

class SQLiteWorkflowExecutionStore final : public WorkflowExecutionStore
{
  public:
    explicit SQLiteWorkflowExecutionStore(SQLiteDatabase& db);

    std::string generateExecutionId() override;

    void save(const WorkflowExecution& execution) override;

    std::optional<WorkflowExecution> find(const std::string& workflowExecutionId) const override;

    void update(const WorkflowExecution& execution) override;

  private:
    SQLiteDatabase& db_;
};

} // namespace workflow::backend::sqlite
