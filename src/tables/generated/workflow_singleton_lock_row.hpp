#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workflow
{

struct WorkflowSingletonLockRow
{
    std::string workflowName;
    std::int64_t workflowVersion;
    std::string workflowExecutionId;
    std::optional<std::string> createdAt;

    friend bool operator==(
        const WorkflowSingletonLockRow&,
        const WorkflowSingletonLockRow&
    ) = default;
};

struct WorkflowSingletonLockRowMapping
{
    static constexpr std::string_view table_name = "workflow_singleton_locks";
    static constexpr int schema_version = 1;
    static constexpr std::string_view key_separator = ":";
    static constexpr std::string_view field_workflowName = "workflowName";
    static constexpr std::string_view field_workflowVersion = "workflowVersion";
    static constexpr std::string_view field_workflowExecutionId = "workflowExecutionId";
    static constexpr std::string_view field_createdAt = "createdAt";
    static constexpr std::string_view key_field = "workflowName:workflowVersion";

    static std::string key(const WorkflowSingletonLockRow& row)
    {
        return row.workflowName + std::string(key_separator) + std::to_string(row.workflowVersion);
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_workflowName)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_workflowVersion)).mark_required(true),
            mt::FieldSpec::string(std::string(field_workflowExecutionId)).mark_required(true),
            mt::FieldSpec::optional(std::string(field_createdAt), mt::FieldType::String)
                .mark_required(true)
        };
    }

    static mt::Json to_json(const WorkflowSingletonLockRow& row)
    {
        return mt::Json::object(
            {{std::string(field_workflowName), row.workflowName},
             {std::string(field_workflowVersion), row.workflowVersion},
             {std::string(field_workflowExecutionId), row.workflowExecutionId},
             {std::string(field_createdAt),
              row.createdAt ? mt::Json(*row.createdAt) : mt::Json::null()}}
        );
    }

    static WorkflowSingletonLockRow from_json(const mt::Json& json)
    {
        return WorkflowSingletonLockRow{
            .workflowName = json[std::string(field_workflowName)].as_string(),
            .workflowVersion = json[std::string(field_workflowVersion)].as_int64(),
            .workflowExecutionId = json[std::string(field_workflowExecutionId)].as_string(),
            .createdAt =
                json[std::string(field_createdAt)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_createdAt)].as_string())
        };
    }
};

} // namespace workflow
