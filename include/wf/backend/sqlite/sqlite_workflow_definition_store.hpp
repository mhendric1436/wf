#pragma once

#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/store/workflow_definition_store.hpp"

#include <mutex>

namespace workflow::backend::sqlite
{

class SQLiteWorkflowDefinitionStore final : public WorkflowDefinitionStore
{
  public:
    explicit SQLiteWorkflowDefinitionStore(SQLiteDatabase& db);

    void save(const WorkflowDefinition& definition) override;

    std::optional<WorkflowDefinition> find(
        const std::string& workflowName,
        int workflowVersion
    ) const override;

    std::vector<WorkflowDefinitionKey> list() const override;

    void remove(
        const std::string& workflowName,
        int workflowVersion
    ) override;

  private:
    SQLiteDatabase& db_;
};

} // namespace workflow::backend::sqlite
