#pragma once

#include "mt/json.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_json.hpp"

#include <string>

namespace workflow
{

struct WorkflowDefinitionMapping
{
    static constexpr std::string_view table_name = "workflow_definitions";

    static std::string key(const WorkflowDefinition& row)
    {
        return row.workflowName + ":" + std::to_string(row.workflowVersion);
    }

    static mt::Json to_json(const WorkflowDefinition& row)
    {
        return toJson(row);
    }

    static WorkflowDefinition from_json(const mt::Json& json)
    {
        return parseWorkflowDefinition(json);
    }
};

} // namespace workflow
