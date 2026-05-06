#pragma once

#include "mt/json.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_json.hpp"

#include <string>

namespace workflow
{

struct WorkflowExecutionMapping
{
    static constexpr std::string_view table_name = "workflow_executions";

    static std::string key(const WorkflowExecution& row)
    {
        return row.workflowExecutionId;
    }

    static mt::Json to_json(const WorkflowExecution& row)
    {
        return toJson(row);
    }

    static WorkflowExecution from_json(const mt::Json& json)
    {
        return workflowExecutionFromJson(json);
    }
};

} // namespace workflow
