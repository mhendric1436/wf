#pragma once

#include "wf/json.hpp"

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
    Failed
};

struct WorkflowExecution
{
    std::string workflowExecutionId;
    std::string workflowName;
    int workflowVersion;

    WorkflowExecutionStatus status = WorkflowExecutionStatus::Running;

    std::string currentStepName;

    json::Value input = json::Value::object();
    json::Value state = json::Value::object();

    int currentStepAttempt = 0;

    std::optional<std::string> failureReason;
};

} // namespace workflow
