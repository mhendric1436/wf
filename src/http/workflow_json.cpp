#include "wf/http/workflow_json.hpp"

#include <cstdio>
#include <ctime>

namespace workflow::http
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
    obj["name"] = step.name;

    if (step.expectedExecutionTime.has_value())
    {
        obj["expectedExecutionTime"] = step.expectedExecutionTime.value();
    }

    if (step.maxRetries.has_value())
    {
        obj["maxRetries"] = step.maxRetries.value();
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
    obj["workflowName"] = def.workflowName;
    obj["workflowVersion"] = def.workflowVersion;
    obj["startWorkflowStepName"] = def.startWorkflowStepName;
    obj["expectedExecutionTime"] = def.expectedExecutionTime;
    obj["steps"] = json::Value(std::move(steps));
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowDefinitionKey& key)
{
    json::Value::Object obj;
    obj["workflowName"] = key.workflowName;
    obj["workflowVersion"] = key.workflowVersion;
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowExecution& exec)
{
    json::Value::Object obj;
    obj["workflowExecutionId"] = exec.workflowExecutionId;
    obj["workflowName"] = exec.workflowName;
    obj["workflowVersion"] = exec.workflowVersion;
    obj["status"] = toString(exec.status);
    obj["currentStepName"] = exec.currentStepName;
    obj["input"] = exec.input;
    obj["state"] = exec.state;
    obj["currentStepAttempt"] = exec.currentStepAttempt;
    obj["failureReason"] =
        exec.failureReason.has_value() ? json::Value(exec.failureReason.value()) : json::Value();
    obj["startedAt"] =
        exec.startedAt.has_value() ? json::Value(toIso8601(exec.startedAt.value())) : json::Value();
    obj["completedAt"] = exec.completedAt.has_value()
                             ? json::Value(toIso8601(exec.completedAt.value()))
                             : json::Value();
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowStepExecution& step)
{
    json::Value::Object obj;
    obj["workflowExecutionId"] = step.workflowExecutionId;
    obj["workflowName"] = step.workflowName;
    obj["workflowVersion"] = step.workflowVersion;
    obj["stepName"] = step.stepName;
    obj["attempt"] = step.attempt;
    obj["status"] = toString(step.status);
    obj["workerId"] =
        step.workerId.has_value() ? json::Value(step.workerId.value()) : json::Value();
    obj["leaseExpiresAt"] = step.leaseExpiresAt.has_value()
                                ? json::Value(toIso8601(step.leaseExpiresAt.value()))
                                : json::Value();
    obj["failureReason"] =
        step.failureReason.has_value() ? json::Value(step.failureReason.value()) : json::Value();
    obj["createdAt"] =
        step.createdAt.has_value() ? json::Value(toIso8601(step.createdAt.value())) : json::Value();
    obj["startedAt"] =
        step.startedAt.has_value() ? json::Value(toIso8601(step.startedAt.value())) : json::Value();
    obj["completedAt"] = step.completedAt.has_value()
                             ? json::Value(toIso8601(step.completedAt.value()))
                             : json::Value();
    obj["input"] = step.input;
    obj["state"] = step.state;
    obj["output"] = step.output;
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
    obj["valid"] = result.valid;
    obj["errors"] = json::Value(std::move(errors));
    return json::Value(std::move(obj));
}

} // namespace workflow::http
