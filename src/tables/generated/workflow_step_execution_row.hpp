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
    static constexpr std::string_view key_field = "workflowExecutionId:stepName:attempt";

    static std::string key(const WorkflowStepExecutionRow& row)
    {
        return row.workflowExecutionId + ":" + row.stepName + ":" + std::to_string(row.attempt);
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string("workflowExecutionId").mark_required(true),
            mt::FieldSpec::string("workflowName").mark_required(true),
            mt::FieldSpec::int64("workflowVersion").mark_required(true),
            mt::FieldSpec::string("stepName").mark_required(true),
            mt::FieldSpec::int64("attempt").mark_required(true),
            mt::FieldSpec::string("status").mark_required(true),
            mt::FieldSpec::optional("workerId", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("leaseExpiresAt", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("failureReason", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("createdAt", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("startedAt", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("completedAt", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::json("input").mark_required(false).with_default(mt::Json::object({})),
            mt::FieldSpec::json("state").mark_required(false).with_default(mt::Json::object({})),
            mt::FieldSpec::json("output").mark_required(false).with_default(mt::Json::object({}))
        };
    }

    static mt::Json to_json(const WorkflowStepExecutionRow& row)
    {
        return mt::Json::object(
            {{"workflowExecutionId", row.workflowExecutionId},
             {"workflowName", row.workflowName},
             {"workflowVersion", row.workflowVersion},
             {"stepName", row.stepName},
             {"attempt", row.attempt},
             {"status", row.status},
             {"workerId", row.workerId ? mt::Json(*row.workerId) : mt::Json::null()},
             {"leaseExpiresAt",
              row.leaseExpiresAt ? mt::Json(*row.leaseExpiresAt) : mt::Json::null()},
             {"failureReason", row.failureReason ? mt::Json(*row.failureReason) : mt::Json::null()},
             {"createdAt", row.createdAt ? mt::Json(*row.createdAt) : mt::Json::null()},
             {"startedAt", row.startedAt ? mt::Json(*row.startedAt) : mt::Json::null()},
             {"completedAt", row.completedAt ? mt::Json(*row.completedAt) : mt::Json::null()},
             {"input", row.input},
             {"state", row.state},
             {"output", row.output}}
        );
    }

    static WorkflowStepExecutionRow from_json(const mt::Json& json)
    {
        return WorkflowStepExecutionRow{
            .workflowExecutionId = json["workflowExecutionId"].as_string(),
            .workflowName = json["workflowName"].as_string(),
            .workflowVersion = json["workflowVersion"].as_int64(),
            .stepName = json["stepName"].as_string(),
            .attempt = json["attempt"].as_int64(),
            .status = json["status"].as_string(),
            .workerId = json["workerId"].is_null()
                            ? std::nullopt
                            : std::optional<std::string>(json["workerId"].as_string()),
            .leaseExpiresAt = json["leaseExpiresAt"].is_null()
                                  ? std::nullopt
                                  : std::optional<std::string>(json["leaseExpiresAt"].as_string()),
            .failureReason = json["failureReason"].is_null()
                                 ? std::nullopt
                                 : std::optional<std::string>(json["failureReason"].as_string()),
            .createdAt = json["createdAt"].is_null()
                             ? std::nullopt
                             : std::optional<std::string>(json["createdAt"].as_string()),
            .startedAt = json["startedAt"].is_null()
                             ? std::nullopt
                             : std::optional<std::string>(json["startedAt"].as_string()),
            .completedAt = json["completedAt"].is_null()
                               ? std::nullopt
                               : std::optional<std::string>(json["completedAt"].as_string()),
            .input = json["input"],
            .state = json["state"],
            .output = json["output"]
        };
    }

    static std::vector<mt::IndexSpec> indexes()
    {
        return {
            mt::IndexSpec::json_path_index("idx_step_workflow_name", "$.workflowName"),
            mt::IndexSpec::json_path_index("idx_step_workflow_version", "$.workflowVersion"),
            mt::IndexSpec::json_path_index("idx_step_status", "$.status")
        };
    }
};

} // namespace workflow
