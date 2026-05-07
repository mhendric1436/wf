#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workflow
{

struct WorkflowStepExecutionRow
{
    std::string workflowExecutionId;
    std::string workflowName;
    std::int64_t workflowVersion;
    std::string stepName;
    std::int64_t attempt;
    std::string status;
    std::optional<std::string> workerId;
    std::optional<std::string> leaseExpiresAt;
    std::optional<std::string> failureReason;
    std::optional<std::string> createdAt;
    std::optional<std::string> startedAt;
    std::optional<std::string> completedAt;
    mt::Json input = mt::Json::object({});
    mt::Json state = mt::Json::object({});
    mt::Json output = mt::Json::object({});

    friend bool operator==(
        const WorkflowStepExecutionRow&,
        const WorkflowStepExecutionRow&
    ) = default;
};

struct WorkflowStepExecutionRowMapping
{
    static constexpr std::string_view table_name = "workflow_step_executions";
    static constexpr int schema_version = 1;
    static constexpr std::string_view key_separator = ":";
    static constexpr std::string_view field_workflowExecutionId = "workflowExecutionId";
    static constexpr std::string_view field_workflowName = "workflowName";
    static constexpr std::string_view field_workflowVersion = "workflowVersion";
    static constexpr std::string_view field_stepName = "stepName";
    static constexpr std::string_view field_attempt = "attempt";
    static constexpr std::string_view field_status = "status";
    static constexpr std::string_view field_workerId = "workerId";
    static constexpr std::string_view field_leaseExpiresAt = "leaseExpiresAt";
    static constexpr std::string_view field_failureReason = "failureReason";
    static constexpr std::string_view field_createdAt = "createdAt";
    static constexpr std::string_view field_startedAt = "startedAt";
    static constexpr std::string_view field_completedAt = "completedAt";
    static constexpr std::string_view field_input = "input";
    static constexpr std::string_view field_state = "state";
    static constexpr std::string_view field_output = "output";
    static constexpr std::string_view key_field = "workflowExecutionId:stepName:attempt";
    static constexpr std::string_view index_0_name = "idx_step_workflow_name";
    static constexpr std::string_view index_0_path = "$.workflowName";
    static constexpr std::string_view index_1_name = "idx_step_workflow_version";
    static constexpr std::string_view index_1_path = "$.workflowVersion";
    static constexpr std::string_view index_2_name = "idx_step_status";
    static constexpr std::string_view index_2_path = "$.status";

    static std::string key(const WorkflowStepExecutionRow& row)
    {
        return row.workflowExecutionId + std::string(key_separator) + row.stepName +
               std::string(key_separator) + std::to_string(row.attempt);
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_workflowExecutionId)).mark_required(true),
            mt::FieldSpec::string(std::string(field_workflowName)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_workflowVersion)).mark_required(true),
            mt::FieldSpec::string(std::string(field_stepName)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_attempt)).mark_required(true),
            mt::FieldSpec::string(std::string(field_status)).mark_required(true),
            mt::FieldSpec::optional(std::string(field_workerId), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_leaseExpiresAt), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_failureReason), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_createdAt), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_startedAt), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::optional(std::string(field_completedAt), mt::FieldType::String)
                .mark_required(true),
            mt::FieldSpec::json(std::string(field_input))
                .mark_required(false)
                .with_default(mt::Json::object({})),
            mt::FieldSpec::json(std::string(field_state))
                .mark_required(false)
                .with_default(mt::Json::object({})),
            mt::FieldSpec::json(std::string(field_output))
                .mark_required(false)
                .with_default(mt::Json::object({}))
        };
    }

    static mt::Json to_json(const WorkflowStepExecutionRow& row)
    {
        return mt::Json::object(
            {{std::string(field_workflowExecutionId), row.workflowExecutionId},
             {std::string(field_workflowName), row.workflowName},
             {std::string(field_workflowVersion), row.workflowVersion},
             {std::string(field_stepName), row.stepName},
             {std::string(field_attempt), row.attempt},
             {std::string(field_status), row.status},
             {std::string(field_workerId),
              row.workerId ? mt::Json(*row.workerId) : mt::Json::null()},
             {std::string(field_leaseExpiresAt),
              row.leaseExpiresAt ? mt::Json(*row.leaseExpiresAt) : mt::Json::null()},
             {std::string(field_failureReason),
              row.failureReason ? mt::Json(*row.failureReason) : mt::Json::null()},
             {std::string(field_createdAt),
              row.createdAt ? mt::Json(*row.createdAt) : mt::Json::null()},
             {std::string(field_startedAt),
              row.startedAt ? mt::Json(*row.startedAt) : mt::Json::null()},
             {std::string(field_completedAt),
              row.completedAt ? mt::Json(*row.completedAt) : mt::Json::null()},
             {std::string(field_input), row.input},
             {std::string(field_state), row.state},
             {std::string(field_output), row.output}}
        );
    }

    static WorkflowStepExecutionRow from_json(const mt::Json& json)
    {
        return WorkflowStepExecutionRow{
            .workflowExecutionId = json[std::string(field_workflowExecutionId)].as_string(),
            .workflowName = json[std::string(field_workflowName)].as_string(),
            .workflowVersion = json[std::string(field_workflowVersion)].as_int64(),
            .stepName = json[std::string(field_stepName)].as_string(),
            .attempt = json[std::string(field_attempt)].as_int64(),
            .status = json[std::string(field_status)].as_string(),
            .workerId =
                json[std::string(field_workerId)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_workerId)].as_string()),
            .leaseExpiresAt = json[std::string(field_leaseExpiresAt)].is_null()
                                  ? std::nullopt
                                  : std::optional<std::string>(
                                        json[std::string(field_leaseExpiresAt)].as_string()
                                    ),
            .failureReason = json[std::string(field_failureReason)].is_null()
                                 ? std::nullopt
                                 : std::optional<std::string>(
                                       json[std::string(field_failureReason)].as_string()
                                   ),
            .createdAt =
                json[std::string(field_createdAt)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_createdAt)].as_string()),
            .startedAt =
                json[std::string(field_startedAt)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_startedAt)].as_string()),
            .completedAt =
                json[std::string(field_completedAt)].is_null()
                    ? std::nullopt
                    : std::optional<std::string>(json[std::string(field_completedAt)].as_string()),
            .input = json[std::string(field_input)],
            .state = json[std::string(field_state)],
            .output = json[std::string(field_output)]
        };
    }

    static std::vector<mt::IndexSpec> indexes()
    {
        return {
            mt::IndexSpec::json_path_index(std::string(index_0_name), std::string(index_0_path)),
            mt::IndexSpec::json_path_index(std::string(index_1_name), std::string(index_1_path)),
            mt::IndexSpec::json_path_index(std::string(index_2_name), std::string(index_2_path))
        };
    }
};

} // namespace workflow
