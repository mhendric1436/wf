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
    static constexpr std::string_view key_field = "workflowExecutionId";

    static std::string key(const WorkflowExecutionRow& row)
    {
        return row.workflowExecutionId;
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string("workflowExecutionId").mark_required(true),
            mt::FieldSpec::string("workflowName").mark_required(true),
            mt::FieldSpec::int64("workflowVersion").mark_required(true),
            mt::FieldSpec::string("status").mark_required(true),
            mt::FieldSpec::string("currentStepName").mark_required(true),
            mt::FieldSpec::json("input").mark_required(false).with_default(mt::Json::object({})),
            mt::FieldSpec::json("state").mark_required(false).with_default(mt::Json::object({})),
            mt::FieldSpec::int64("currentStepAttempt")
                .mark_required(false)
                .with_default(mt::Json(std::int64_t{0})),
            mt::FieldSpec::optional("failureReason", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("startedAt", mt::FieldType::String).mark_required(true),
            mt::FieldSpec::optional("completedAt", mt::FieldType::String).mark_required(true)
        };
    }

    static mt::Json to_json(const WorkflowExecutionRow& row)
    {
        return mt::Json::object(
            {{"workflowExecutionId", row.workflowExecutionId},
             {"workflowName", row.workflowName},
             {"workflowVersion", row.workflowVersion},
             {"status", row.status},
             {"currentStepName", row.currentStepName},
             {"input", row.input},
             {"state", row.state},
             {"currentStepAttempt", row.currentStepAttempt},
             {"failureReason", row.failureReason ? mt::Json(*row.failureReason) : mt::Json::null()},
             {"startedAt", row.startedAt ? mt::Json(*row.startedAt) : mt::Json::null()},
             {"completedAt", row.completedAt ? mt::Json(*row.completedAt) : mt::Json::null()}}
        );
    }

    static WorkflowExecutionRow from_json(const mt::Json& json)
    {
        return WorkflowExecutionRow{
            .workflowExecutionId = json["workflowExecutionId"].as_string(),
            .workflowName = json["workflowName"].as_string(),
            .workflowVersion = json["workflowVersion"].as_int64(),
            .status = json["status"].as_string(),
            .currentStepName = json["currentStepName"].as_string(),
            .input = json["input"],
            .state = json["state"],
            .currentStepAttempt = json["currentStepAttempt"].as_int64(),
            .failureReason = json["failureReason"].is_null()
                                 ? std::nullopt
                                 : std::optional<std::string>(json["failureReason"].as_string()),
            .startedAt = json["startedAt"].is_null()
                             ? std::nullopt
                             : std::optional<std::string>(json["startedAt"].as_string()),
            .completedAt = json["completedAt"].is_null()
                               ? std::nullopt
                               : std::optional<std::string>(json["completedAt"].as_string())
        };
    }
};

} // namespace workflow
