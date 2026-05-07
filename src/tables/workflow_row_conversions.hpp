#pragma once

#include "generated/workflow_definition_row.hpp"
#include "generated/workflow_execution_row.hpp"
#include "generated/workflow_step_execution_row.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_json.hpp"
#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace workflow
{

inline std::optional<std::string>
toStorageTime(const std::optional<std::chrono::system_clock::time_point>& value)
{
    if (!value.has_value())
    {
        return std::nullopt;
    }

    return toIso8601(value.value());
}

inline std::optional<std::chrono::system_clock::time_point>
fromStorageTime(const std::optional<std::string>& value)
{
    if (!value.has_value())
    {
        return std::nullopt;
    }

    return fromIso8601(value.value());
}

inline WorkflowDefinitionStepRow toRow(const WorkflowStep& step)
{
    return WorkflowDefinitionStepRow{
        .name = step.name,
        .expectedExecutionTime = step.expectedExecutionTime,
        .maxRetries = step.maxRetries.has_value()
                          ? std::optional<std::int64_t>{step.maxRetries.value()}
                          : std::nullopt,
        .additionalFields = mt::Json(step.additionalFields),
    };
}

inline WorkflowStep fromRow(const WorkflowDefinitionStepRow& row)
{
    WorkflowStep step;
    step.name = row.name;
    step.expectedExecutionTime = row.expectedExecutionTime;
    step.maxRetries = row.maxRetries.has_value()
                          ? std::optional<int>{static_cast<int>(row.maxRetries.value())}
                          : std::nullopt;

    if (row.additionalFields.is_object())
    {
        step.additionalFields = row.additionalFields.as_object();
    }

    return step;
}

inline WorkflowDefinitionRow toRow(const WorkflowDefinition& definition)
{
    WorkflowDefinitionRow row;
    row.workflowName = definition.workflowName;
    row.workflowVersion = definition.workflowVersion;
    row.startWorkflowStepName = definition.startWorkflowStepName;
    row.expectedExecutionTime = definition.expectedExecutionTime;
    row.singleton = definition.singleton;
    row.steps.reserve(definition.steps.size());

    for (const auto& step : definition.steps)
    {
        row.steps.push_back(toRow(step));
    }

    return row;
}

inline WorkflowDefinition fromRow(const WorkflowDefinitionRow& row)
{
    WorkflowDefinition definition;
    definition.workflowName = row.workflowName;
    definition.workflowVersion = static_cast<int>(row.workflowVersion);
    definition.startWorkflowStepName = row.startWorkflowStepName;
    definition.expectedExecutionTime = row.expectedExecutionTime;
    definition.singleton = row.singleton.value_or(false);
    definition.steps.reserve(row.steps.size());

    for (const auto& step : row.steps)
    {
        definition.steps.push_back(fromRow(step));
    }

    return definition;
}

inline WorkflowExecutionRow toRow(const WorkflowExecution& execution)
{
    return WorkflowExecutionRow{
        .workflowExecutionId = execution.workflowExecutionId,
        .workflowName = execution.workflowName,
        .workflowVersion = execution.workflowVersion,
        .status = toString(execution.status),
        .currentStepName = execution.currentStepName,
        .input = execution.input,
        .state = execution.state,
        .currentStepAttempt = execution.currentStepAttempt,
        .failureReason = execution.failureReason,
        .startedAt = toStorageTime(execution.startedAt),
        .completedAt = toStorageTime(execution.completedAt),
    };
}

inline WorkflowExecution fromRow(const WorkflowExecutionRow& row)
{
    WorkflowExecution execution;
    execution.workflowExecutionId = row.workflowExecutionId;
    execution.workflowName = row.workflowName;
    execution.workflowVersion = static_cast<int>(row.workflowVersion);
    execution.status = executionStatusFromString(row.status);
    execution.currentStepName = row.currentStepName;
    execution.input = row.input;
    execution.state = row.state;
    execution.currentStepAttempt = static_cast<int>(row.currentStepAttempt);
    execution.failureReason = row.failureReason;
    execution.startedAt = fromStorageTime(row.startedAt);
    execution.completedAt = fromStorageTime(row.completedAt);
    return execution;
}

inline WorkflowStepExecutionRow toRow(const WorkflowStepExecution& stepExecution)
{
    return WorkflowStepExecutionRow{
        .workflowExecutionId = stepExecution.workflowExecutionId,
        .workflowName = stepExecution.workflowName,
        .workflowVersion = stepExecution.workflowVersion,
        .stepName = stepExecution.stepName,
        .attempt = stepExecution.attempt,
        .status = toString(stepExecution.status),
        .workerId = stepExecution.workerId,
        .leaseExpiresAt = toStorageTime(stepExecution.leaseExpiresAt),
        .failureReason = stepExecution.failureReason,
        .createdAt = toStorageTime(stepExecution.createdAt),
        .scheduledAt = toStorageTime(stepExecution.scheduledAt),
        .startedAt = toStorageTime(stepExecution.startedAt),
        .completedAt = toStorageTime(stepExecution.completedAt),
        .input = stepExecution.input,
        .state = stepExecution.state,
        .output = stepExecution.output,
    };
}

inline WorkflowStepExecution fromRow(const WorkflowStepExecutionRow& row)
{
    WorkflowStepExecution stepExecution;
    stepExecution.workflowExecutionId = row.workflowExecutionId;
    stepExecution.workflowName = row.workflowName;
    stepExecution.workflowVersion = static_cast<int>(row.workflowVersion);
    stepExecution.stepName = row.stepName;
    stepExecution.attempt = static_cast<int>(row.attempt);
    stepExecution.status = stepStatusFromString(row.status);
    stepExecution.workerId = row.workerId;
    stepExecution.leaseExpiresAt = fromStorageTime(row.leaseExpiresAt);
    stepExecution.failureReason = row.failureReason;
    stepExecution.createdAt = fromStorageTime(row.createdAt);
    stepExecution.scheduledAt = fromStorageTime(row.scheduledAt);
    stepExecution.startedAt = fromStorageTime(row.startedAt);
    stepExecution.completedAt = fromStorageTime(row.completedAt);
    stepExecution.input = row.input;
    stepExecution.state = row.state;
    stepExecution.output = row.output;
    return stepExecution;
}

} // namespace workflow
