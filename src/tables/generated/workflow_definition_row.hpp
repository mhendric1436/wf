#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace workflow
{

struct WorkflowDefinitionStepRow
{
    std::string name;
    std::optional<std::string> expectedExecutionTime;
    std::optional<std::int64_t> maxRetries;
    mt::Json additionalFields = mt::Json::object({});

    friend bool operator==(
        const WorkflowDefinitionStepRow&,
        const WorkflowDefinitionStepRow&
    ) = default;
};

struct WorkflowDefinitionRow
{
    std::string workflowName;
    std::int64_t workflowVersion;
    std::string startWorkflowStepName;
    std::string expectedExecutionTime;
    std::vector<WorkflowDefinitionStepRow> steps;

    friend bool operator==(
        const WorkflowDefinitionRow&,
        const WorkflowDefinitionRow&
    ) = default;
};

struct WorkflowDefinitionRowMapping
{
    static constexpr std::string_view table_name = "workflow_definitions";
    static constexpr int schema_version = 1;
    static constexpr std::string_view key_field = "workflowName:workflowVersion";

    static std::string key(const WorkflowDefinitionRow& row)
    {
        return row.workflowName + ":" + std::to_string(row.workflowVersion);
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string("workflowName").mark_required(true),
            mt::FieldSpec::int64("workflowVersion").mark_required(true),
            mt::FieldSpec::string("startWorkflowStepName").mark_required(true),
            mt::FieldSpec::string("expectedExecutionTime").mark_required(true),
            mt::FieldSpec::array_object(
                "steps",
                {mt::FieldSpec::string("name").mark_required(true),
                 mt::FieldSpec::optional("expectedExecutionTime", mt::FieldType::String)
                     .mark_required(true),
                 mt::FieldSpec::optional("maxRetries", mt::FieldType::Int64).mark_required(true),
                 mt::FieldSpec::json("additionalFields")
                     .mark_required(false)
                     .with_default(mt::Json::object({}))}
            )
                .mark_required(true)
        };
    }

    static mt::Json to_json(const WorkflowDefinitionRow& row)
    {
        return mt::Json::object(
            {{"workflowName", row.workflowName},
             {"workflowVersion", row.workflowVersion},
             {"startWorkflowStepName", row.startWorkflowStepName},
             {"expectedExecutionTime", row.expectedExecutionTime},
             {"steps", to_json_array_WorkflowDefinitionStepRow(row.steps)}}
        );
    }

    static mt::Json
    to_json_array_WorkflowDefinitionStepRow(const std::vector<WorkflowDefinitionStepRow>& values)
    {
        mt::Json::Array array;
        array.reserve(values.size());
        for (const auto& value : values)
        {
            array.push_back(to_json(value));
        }
        return mt::Json::array(std::move(array));
    }

    static std::vector<WorkflowDefinitionStepRow>
    from_json_array_WorkflowDefinitionStepRow(const mt::Json& json)
    {
        std::vector<WorkflowDefinitionStepRow> values;
        const auto& array = json.as_array();
        values.reserve(array.size());
        for (const auto& item : array)
        {
            values.push_back(from_json_WorkflowDefinitionStepRow(item));
        }
        return values;
    }

    static mt::Json to_json(const WorkflowDefinitionStepRow& row)
    {
        return mt::Json::object(
            {{"name", row.name},
             {"expectedExecutionTime",
              row.expectedExecutionTime ? mt::Json(*row.expectedExecutionTime) : mt::Json::null()},
             {"maxRetries", row.maxRetries ? mt::Json(*row.maxRetries) : mt::Json::null()},
             {"additionalFields", row.additionalFields}}
        );
    }

    static WorkflowDefinitionStepRow from_json_WorkflowDefinitionStepRow(const mt::Json& json)
    {
        return WorkflowDefinitionStepRow{
            .name = json["name"].as_string(),
            .expectedExecutionTime =
                json["expectedExecutionTime"].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json["expectedExecutionTime"].as_string()),
            .maxRetries = json["maxRetries"].is_null()
                              ? std::nullopt
                              : std::optional<std::int64_t>(json["maxRetries"].as_int64()),
            .additionalFields = json["additionalFields"]
        };
    }

    static WorkflowDefinitionRow from_json(const mt::Json& json)
    {
        return WorkflowDefinitionRow{
            .workflowName = json["workflowName"].as_string(),
            .workflowVersion = json["workflowVersion"].as_int64(),
            .startWorkflowStepName = json["startWorkflowStepName"].as_string(),
            .expectedExecutionTime = json["expectedExecutionTime"].as_string(),
            .steps = from_json_array_WorkflowDefinitionStepRow(json["steps"])
        };
    }
};

} // namespace workflow
