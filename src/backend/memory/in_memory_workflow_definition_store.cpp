#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"

#include <stdexcept>

namespace workflow::backend::memory
{

void InMemoryWorkflowDefinitionStore::save(const WorkflowDefinition& definition)
{
    validateKey(definition.workflowName, definition.workflowVersion);

    definitions_[makeKey(definition.workflowName, definition.workflowVersion)] = definition;
}

std::optional<WorkflowDefinition> InMemoryWorkflowDefinitionStore::find(
    const std::string& workflowName,
    int workflowVersion
) const
{
    validateKey(workflowName, workflowVersion);

    const auto iter = definitions_.find(makeKey(workflowName, workflowVersion));

    if (iter == definitions_.end())
    {
        return std::nullopt;
    }

    return iter->second;
}

std::vector<WorkflowDefinitionKey> InMemoryWorkflowDefinitionStore::list() const
{
    std::vector<WorkflowDefinitionKey> keys;
    keys.reserve(definitions_.size());

    for (const auto& [key, definition] : definitions_)
    {
        (void)definition;

        keys.push_back(
            WorkflowDefinitionKey{
                .workflowName = key.first,
                .workflowVersion = key.second,
            }
        );
    }

    return keys;
}

void InMemoryWorkflowDefinitionStore::remove(
    const std::string& workflowName,
    int workflowVersion
)
{
    validateKey(workflowName, workflowVersion);

    definitions_.erase(makeKey(workflowName, workflowVersion));
}

void InMemoryWorkflowDefinitionStore::clear()
{
    definitions_.clear();
}

std::size_t InMemoryWorkflowDefinitionStore::size() const
{
    return definitions_.size();
}

InMemoryWorkflowDefinitionStore::Key InMemoryWorkflowDefinitionStore::makeKey(
    const std::string& workflowName,
    int workflowVersion
)
{
    return Key{workflowName, workflowVersion};
}

void InMemoryWorkflowDefinitionStore::validateKey(
    const std::string& workflowName,
    int workflowVersion
)
{
    if (workflowName.empty())
    {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (workflowVersion < 1)
    {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }
}

} // namespace workflow::backend::memory
