#pragma once

#include "mt/json.hpp"
#include "mt/query.hpp"
#include "wf/workflow_json.hpp"
#include "wf/workflow_step_execution.hpp"

#include <string>
#include <vector>

namespace workflow
{

struct WorkflowStepExecutionMapping
{
    static constexpr std::string_view table_name = "workflow_step_executions";

    static std::string key(const WorkflowStepExecution& row)
    {
        return row.workflowExecutionId + ":" + row.stepName + ":" + std::to_string(row.attempt);
    }

    static mt::Json to_json(const WorkflowStepExecution& row)
    {
        return toJson(row);
    }

    static WorkflowStepExecution from_json(const mt::Json& json)
    {
        return workflowStepExecutionFromJson(json);
    }

    static std::vector<mt::IndexSpec> indexes()
    {
        return {
            mt::IndexSpec::json_path_index("idx_step_workflow_name", "$.workflowName"),
            mt::IndexSpec::json_path_index("idx_step_workflow_version", "$.workflowVersion"),
            mt::IndexSpec::json_path_index("idx_step_status", "$.status"),
        };
    }
};

} // namespace workflow
