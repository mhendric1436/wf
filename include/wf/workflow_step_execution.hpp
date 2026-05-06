#pragma once

#include "mt/json.hpp"
#include "wf/workflow_execution.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace workflow
{

struct WorkflowStepExecution
{
    std::string workflowExecutionId;
    std::string workflowName;
    int workflowVersion = 0;

    std::string stepName;
    int attempt = 0;

    StepExecutionStatus status = StepExecutionStatus::Pending;

    std::optional<std::string> workerId;
    std::optional<std::chrono::system_clock::time_point> leaseExpiresAt;
    std::optional<std::string> failureReason;

    std::optional<std::chrono::system_clock::time_point> createdAt;
    std::optional<std::chrono::system_clock::time_point> startedAt;
    std::optional<std::chrono::system_clock::time_point> completedAt;

    mt::Json input = mt::Json(mt::Json::Object{});
    mt::Json state = mt::Json(mt::Json::Object{});
    mt::Json output = mt::Json(mt::Json::Object{});
};

} // namespace workflow
