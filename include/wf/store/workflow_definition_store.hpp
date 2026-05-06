#pragma once

#include "wf/workflow_definition.hpp"

#include <optional>
#include <string>
#include <vector>

namespace workflow
{

class WorkflowDefinitionStore
{
  public:
    virtual ~WorkflowDefinitionStore() = default;

    virtual void save(const WorkflowDefinition& definition) = 0;

    virtual std::optional<WorkflowDefinition> find(
        const std::string& workflowName,
        int workflowVersion
    ) const = 0;

    virtual std::vector<WorkflowDefinitionKey> list() const = 0;

    virtual void remove(
        const std::string& workflowName,
        int workflowVersion
    ) = 0;
};

} // namespace workflow
