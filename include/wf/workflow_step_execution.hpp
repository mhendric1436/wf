#pragma once

#include "wf/json.hpp"
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

    json::Value input = json::Value::object();
    json::Value state = json::Value::object();
    json::Value output = json::Value::object();
};

} // namespace workflow
