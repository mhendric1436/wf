#pragma once

#include "mt/json.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace workflow
{

enum class WorkflowExecutionStatus
{
    Running,
    Completed,
    Failed,
    Canceled
};

enum class StepExecutionStatus
{
    Pending,
    Running,
    Completed,
    Failed,
    Canceled
};

struct WorkflowExecution
{
    std::string workflowExecutionId;
    std::string workflowName;
    int workflowVersion = 0;

    WorkflowExecutionStatus status = WorkflowExecutionStatus::Running;

    std::string currentStepName;

    mt::Json input = mt::Json(mt::Json::Object{});
    mt::Json state = mt::Json(mt::Json::Object{});

    int currentStepAttempt = 0;

    std::optional<std::string> failureReason;

    std::optional<std::chrono::system_clock::time_point> startedAt;
    std::optional<std::chrono::system_clock::time_point> completedAt;
};

} // namespace workflow
