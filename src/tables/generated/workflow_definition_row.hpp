#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <optional>
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
    std::optional<bool> singleton;
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
    static constexpr std::string_view key_separator = ":";
    static constexpr std::string_view field_workflowName = "workflowName";
    static constexpr std::string_view field_workflowVersion = "workflowVersion";
    static constexpr std::string_view field_startWorkflowStepName = "startWorkflowStepName";
    static constexpr std::string_view field_expectedExecutionTime = "expectedExecutionTime";
    static constexpr std::string_view field_singleton = "singleton";
    static constexpr std::string_view field_steps = "steps";
    static constexpr std::string_view field_name = "name";
    static constexpr std::string_view field_maxRetries = "maxRetries";
    static constexpr std::string_view field_additionalFields = "additionalFields";
    static constexpr std::string_view key_field = "workflowName:workflowVersion";

    static std::string key(const WorkflowDefinitionRow& row)
    {
        return row.workflowName + std::string(key_separator) + std::to_string(row.workflowVersion);
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_workflowName)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_workflowVersion)).mark_required(true),
            mt::FieldSpec::string(std::string(field_startWorkflowStepName)).mark_required(true),
            mt::FieldSpec::string(std::string(field_expectedExecutionTime)).mark_required(true),
            mt::FieldSpec::optional(std::string(field_singleton), mt::FieldType::Bool)
                .mark_required(true),
            mt::FieldSpec::array_object(
                std::string(field_steps),
                {mt::FieldSpec::string(std::string(field_name)).mark_required(true),
                 mt::FieldSpec::optional(
                     std::string(field_expectedExecutionTime), mt::FieldType::String
                 )
                     .mark_required(true),
                 mt::FieldSpec::optional(std::string(field_maxRetries), mt::FieldType::Int64)
                     .mark_required(true),
                 mt::FieldSpec::json(std::string(field_additionalFields))
                     .mark_required(false)
                     .with_default(mt::Json::object({}))}
            )
                .mark_required(true)
        };
    }

    static mt::Json to_json(const WorkflowDefinitionRow& row)
    {
        return mt::Json::object(
            {{std::string(field_workflowName), row.workflowName},
             {std::string(field_workflowVersion), row.workflowVersion},
             {std::string(field_startWorkflowStepName), row.startWorkflowStepName},
             {std::string(field_expectedExecutionTime), row.expectedExecutionTime},
             {std::string(field_singleton),
              row.singleton ? mt::Json(*row.singleton) : mt::Json::null()},
             {std::string(field_steps), to_json_array_WorkflowDefinitionStepRow(row.steps)}}
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
            {{std::string(field_name), row.name},
             {std::string(field_expectedExecutionTime),
              row.expectedExecutionTime ? mt::Json(*row.expectedExecutionTime) : mt::Json::null()},
             {std::string(field_maxRetries),
              row.maxRetries ? mt::Json(*row.maxRetries) : mt::Json::null()},
             {std::string(field_additionalFields), row.additionalFields}}
        );
    }

    static WorkflowDefinitionStepRow from_json_WorkflowDefinitionStepRow(const mt::Json& json)
    {
        return WorkflowDefinitionStepRow{
            .name = json[std::string(field_name)].as_string(),
            .expectedExecutionTime =
                json[std::string(field_expectedExecutionTime)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(
                          json[std::string(field_expectedExecutionTime)].as_string()
                      ),
            .maxRetries =
                json[std::string(field_maxRetries)].is_null()
                    ? std::nullopt
                    : std::optional<std::int64_t>(json[std::string(field_maxRetries)].as_int64()),
            .additionalFields = json[std::string(field_additionalFields)]
        };
    }

    static WorkflowDefinitionRow from_json(const mt::Json& json)
    {
        return WorkflowDefinitionRow{
            .workflowName = json[std::string(field_workflowName)].as_string(),
            .workflowVersion = json[std::string(field_workflowVersion)].as_int64(),
            .startWorkflowStepName = json[std::string(field_startWorkflowStepName)].as_string(),
            .expectedExecutionTime = json[std::string(field_expectedExecutionTime)].as_string(),
            .singleton = json[std::string(field_singleton)].is_null()
                             ? std::nullopt
                             : std::optional<bool>(json[std::string(field_singleton)].as_bool()),
            .steps = from_json_array_WorkflowDefinitionStepRow(json[std::string(field_steps)])
        };
    }
};

} // namespace workflow
