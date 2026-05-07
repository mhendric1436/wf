#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workflow
{

struct WorkflowExecutionRow
{
    std::string workflowExecutionId;
    std::string workflowName;
    std::int64_t workflowVersion;
    std::string status;
    std::string currentStepName;
    mt::Json input = mt::Json::object({});
    mt::Json state = mt::Json::object({});
    std::int64_t currentStepAttempt = 0;
    std::optional<std::string> failureReason;
    std::optional<std::string> startedAt;
    std::optional<std::string> completedAt;

    friend bool operator==(
        const WorkflowExecutionRow&,
        const WorkflowExecutionRow&
    ) = default;
};

struct WorkflowExecutionRowMapping
{
    static constexpr std::string_view table_name = "workflow_executions";
    static constexpr int schema_version = 1;
    static constexpr std::string_view field_workflowExecutionId = "workflowExecutionId";
    static constexpr std::string_view field_workflowName = "workflowName";
    static constexpr std::string_view field_workflowVersion = "workflowVersion";
    static constexpr std::string_view field_status = "status";
    static constexpr std::string_view field_currentStepName = "currentStepName";
    static constexpr std::string_view field_input = "input";
    static constexpr std::string_view field_state = "state";
    static constexpr std::string_view field_currentStepAttempt = "currentStepAttempt";
    static constexpr std::string_view field_failureReason = "failureReason";
    static constexpr std::string_view field_startedAt = "startedAt";
    static constexpr std::string_view field_completedAt = "completedAt";
    static constexpr std::string_view key_field = field_workflowExecutionId;

    static std::string key(const WorkflowExecutionRow& row)
    {
        return row.workflowExecutionId;
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_workflowExecutionId)).mark_required(true),
            mt::FieldSpec::string(std::string(field_workflowName)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_workflowVersion)).mark_required(true),
            mt::FieldSpec::string(std::string(field_status)).mark_required(true),
            mt::FieldSpec::string(std::string(field_currentStepName)).mark_required(true),
            mt::FieldSpec::json(std::string(field_input))
                .mark_required(false)
                .with_default(mt::Json::object({})),
            mt::FieldSpec::json(std::string(field_state))
                .mark_required(false)
                .with_default(mt::Json::object({})),
            mt::FieldSpec::int64(std::string(field_currentStepAttempt))
                .mark_required(false)
                .with_default(mt::Json(std::int64_t{0})),
            mt::FieldSpec::optional(std::string(field_failureReason), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_startedAt), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_completedAt), mt::FieldType::String)
                .mark_required(true)
        };
    }

    static mt::Json to_json(const WorkflowExecutionRow& row)
    {
        return mt::Json::object(
            {{std::string(field_workflowExecutionId), row.workflowExecutionId},
             {std::string(field_workflowName), row.workflowName},
             {std::string(field_workflowVersion), row.workflowVersion},
             {std::string(field_status), row.status},
             {std::string(field_currentStepName), row.currentStepName},
             {std::string(field_input), row.input},
             {std::string(field_state), row.state},
             {std::string(field_currentStepAttempt), row.currentStepAttempt},
             {std::string(field_failureReason),
              row.failureReason ? mt::Json(*row.failureReason) : mt::Json::null()},
             {std::string(field_startedAt),
              row.startedAt ? mt::Json(*row.startedAt) : mt::Json::null()},
             {std::string(field_completedAt),
              row.completedAt ? mt::Json(*row.completedAt) : mt::Json::null()}}
        );
    }

    static WorkflowExecutionRow from_json(const mt::Json& json)
    {
        return WorkflowExecutionRow{
            .workflowExecutionId = json[std::string(field_workflowExecutionId)].as_string(),
            .workflowName = json[std::string(field_workflowName)].as_string(),
            .workflowVersion = json[std::string(field_workflowVersion)].as_int64(),
            .status = json[std::string(field_status)].as_string(),
            .currentStepName = json[std::string(field_currentStepName)].as_string(),
            .input = json[std::string(field_input)],
            .state = json[std::string(field_state)],
            .currentStepAttempt = json[std::string(field_currentStepAttempt)].as_int64(),
            .failureReason = json[std::string(field_failureReason)].is_null()
                                 ? std::nullopt
                                 : std::optional<std::string>(
                                       json[std::string(field_failureReason)].as_string()
                                   ),
            .startedAt =
                json[std::string(field_startedAt)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_startedAt)].as_string()),
            .completedAt =
                json[std::string(field_completedAt)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_completedAt)].as_string())
        };
    }
};

} // namespace workflow
