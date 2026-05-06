#include "wf/workflow_json.hpp"

#include <cstdio>
#include <ctime>

namespace
{

constexpr const char* kAttempt = "attempt";
constexpr const char* kCompletedAt = "completedAt";
constexpr const char* kCreatedAt = "createdAt";
constexpr const char* kCurrentStepAttempt = "currentStepAttempt";
constexpr const char* kCurrentStepName = "currentStepName";
constexpr const char* kErrors = "errors";
constexpr const char* kExpectedExecutionTime = "expectedExecutionTime";
constexpr const char* kFailureReason = "failureReason";
constexpr const char* kInput = "input";
constexpr const char* kLeaseExpiresAt = "leaseExpiresAt";
constexpr const char* kMaxRetries = "maxRetries";
constexpr const char* kName = "name";
constexpr const char* kOutput = "output";
constexpr const char* kStartedAt = "startedAt";
constexpr const char* kStartWorkflowStepName = "startWorkflowStepName";
constexpr const char* kState = "state";
constexpr const char* kStatus = "status";
constexpr const char* kStepName = "stepName";
constexpr const char* kSteps = "steps";
constexpr const char* kValid = "valid";
constexpr const char* kWorkerId = "workerId";
constexpr const char* kWorkflowExecutionId = "workflowExecutionId";
constexpr const char* kWorkflowName = "workflowName";
constexpr const char* kWorkflowVersion = "workflowVersion";

} // namespace

namespace workflow
{

std::string toIso8601(std::chrono::system_clock::time_point tp)
{
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::tm tm{};
    gmtime_r(&tt, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    char result[40];
    std::snprintf(result, sizeof(result), "%s.%03ldZ", buf, static_cast<long>(ms.count()));
    return result;
}

std::chrono::system_clock::time_point fromIso8601(const std::string& s)
{
    std::tm tm{};
    int ms = 0;

    std::sscanf(
        s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
        &tm.tm_min, &tm.tm_sec, &ms
    );

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = 0;

    const auto tt = timegm(&tm);
    return std::chrono::system_clock::from_time_t(tt) + std::chrono::milliseconds(ms);
}

std::string toString(WorkflowExecutionStatus status)
{
    switch (status)
    {
    case WorkflowExecutionStatus::Running:
        return "Running";
    case WorkflowExecutionStatus::Completed:
        return "Completed";
    case WorkflowExecutionStatus::Failed:
        return "Failed";
    case WorkflowExecutionStatus::Canceled:
        return "Canceled";
    }
    return "Running";
}

std::string toString(StepExecutionStatus status)
{
    switch (status)
    {
    case StepExecutionStatus::Pending:
        return "Pending";
    case StepExecutionStatus::Running:
        return "Running";
    case StepExecutionStatus::Completed:
        return "Completed";
    case StepExecutionStatus::Failed:
        return "Failed";
    case StepExecutionStatus::Canceled:
        return "Canceled";
    }
    return "Pending";
}

json::Value toJson(const WorkflowStep& step)
{
    json::Value::Object obj;
    obj[kName] = step.name;

    if (step.expectedExecutionTime.has_value())
    {
        obj[kExpectedExecutionTime] = step.expectedExecutionTime.value();
    }

    if (step.maxRetries.has_value())
    {
        obj[kMaxRetries] = step.maxRetries.value();
    }

    for (const auto& [k, v] : step.additionalFields)
    {
        obj[k] = v;
    }

    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowDefinition& def)
{
    json::Value::Array steps;
    for (const auto& step : def.steps)
    {
        steps.push_back(toJson(step));
    }

    json::Value::Object obj;
    obj[kWorkflowName] = def.workflowName;
    obj[kWorkflowVersion] = def.workflowVersion;
    obj[kStartWorkflowStepName] = def.startWorkflowStepName;
    obj[kExpectedExecutionTime] = def.expectedExecutionTime;
    obj[kSteps] = json::Value(std::move(steps));
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowDefinitionKey& key)
{
    json::Value::Object obj;
    obj[kWorkflowName] = key.workflowName;
    obj[kWorkflowVersion] = key.workflowVersion;
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowExecution& exec)
{
    json::Value::Object obj;
    obj[kWorkflowExecutionId] = exec.workflowExecutionId;
    obj[kWorkflowName] = exec.workflowName;
    obj[kWorkflowVersion] = exec.workflowVersion;
    obj[kStatus] = toString(exec.status);
    obj[kCurrentStepName] = exec.currentStepName;
    obj[kInput] = exec.input;
    obj[kState] = exec.state;
    obj[kCurrentStepAttempt] = exec.currentStepAttempt;
    obj[kFailureReason] =
        exec.failureReason.has_value() ? json::Value(exec.failureReason.value()) : json::Value();
    obj[kStartedAt] =
        exec.startedAt.has_value() ? json::Value(toIso8601(exec.startedAt.value())) : json::Value();
    obj[kCompletedAt] = exec.completedAt.has_value()
                            ? json::Value(toIso8601(exec.completedAt.value()))
                            : json::Value();
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowStepExecution& step)
{
    json::Value::Object obj;
    obj[kWorkflowExecutionId] = step.workflowExecutionId;
    obj[kWorkflowName] = step.workflowName;
    obj[kWorkflowVersion] = step.workflowVersion;
    obj[kStepName] = step.stepName;
    obj[kAttempt] = step.attempt;
    obj[kStatus] = toString(step.status);
    obj[kWorkerId] = step.workerId.has_value() ? json::Value(step.workerId.value()) : json::Value();
    obj[kLeaseExpiresAt] = step.leaseExpiresAt.has_value()
                               ? json::Value(toIso8601(step.leaseExpiresAt.value()))
                               : json::Value();
    obj[kFailureReason] =
        step.failureReason.has_value() ? json::Value(step.failureReason.value()) : json::Value();
    obj[kCreatedAt] =
        step.createdAt.has_value() ? json::Value(toIso8601(step.createdAt.value())) : json::Value();
    obj[kStartedAt] =
        step.startedAt.has_value() ? json::Value(toIso8601(step.startedAt.value())) : json::Value();
    obj[kCompletedAt] = step.completedAt.has_value()
                            ? json::Value(toIso8601(step.completedAt.value()))
                            : json::Value();
    obj[kInput] = step.input;
    obj[kState] = step.state;
    obj[kOutput] = step.output;
    return json::Value(std::move(obj));
}

json::Value toJson(const ValidationResult& result)
{
    json::Value::Array errors;
    for (const auto& e : result.errors)
    {
        errors.push_back(json::Value(e));
    }

    json::Value::Object obj;
    obj[kValid] = result.valid;
    obj[kErrors] = json::Value(std::move(errors));
    return json::Value(std::move(obj));
}

} // namespace workflow
