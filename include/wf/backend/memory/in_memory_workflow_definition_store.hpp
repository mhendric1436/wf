#pragma once

#include "wf/store/workflow_definition_store.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace workflow::backend::memory
{

class InMemoryWorkflowDefinitionStore final : public workflow::WorkflowDefinitionStore
{
  public:
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

    void clear();

    std::size_t size() const;

  private:
    using Key = std::pair<std::string, int>;

    static Key makeKey(
        const std::string& workflowName,
        int workflowVersion
    );
    static void validateKey(
        const std::string& workflowName,
        int workflowVersion
    );

    std::map<Key, WorkflowDefinition> definitions_;
};

} // namespace workflow::backend::memory
