#include "wf/workflow_json.hpp"

#include <cstdio>
#include <ctime>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

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

constexpr const char* kRootPath = "$";
constexpr const char* NAME_PATTERN_STRING = "^[A-Za-z][A-Za-z0-9_-]*$";

const std::regex NAME_PATTERN(NAME_PATTERN_STRING);

const std::regex ISO_8601_DURATION_PATTERN(
    "^P(?!$)"
    "(\\d+Y)?"
    "(\\d+M)?"
    "(\\d+W)?"
    "(\\d+D)?"
    "(T(?!$)"
    "(\\d+H)?"
    "(\\d+M)?"
    "(\\d+S)?"
    ")?$"
);

bool isNonEmptyString(const workflow::json::Value& value)
{
    return value.isString() && !value.asString().empty();
}

bool isValidName(const std::string& value)
{
    return std::regex_match(value, NAME_PATTERN);
}

bool isValidDuration(const std::string& value)
{
    return std::regex_match(value, ISO_8601_DURATION_PATTERN);
}

void requireObject(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& path
)
{
    if (!value.isObject())
    {
        result.addError(path + " must be an object");
    }
}

void requireField(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    if (!value.contains(fieldName))
    {
        result.addError(path + "." + fieldName + " is required");
    }
}

void validateRequiredStringField(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    requireField(value, result, fieldName, path);

    if (!value.contains(fieldName))
    {
        return;
    }

    if (!isNonEmptyString(value.at(fieldName)))
    {
        result.addError(path + "." + fieldName + " must be a non-empty string");
    }
}

void validateRequiredNameField(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    validateRequiredStringField(value, result, fieldName, path);

    if (!value.contains(fieldName) || !value.at(fieldName).isString())
    {
        return;
    }

    const auto name = value.at(fieldName).asString();

    if (!isValidName(name))
    {
        result.addError(path + "." + fieldName + " must match pattern " + NAME_PATTERN_STRING);
    }
}

void validateRequiredDurationField(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    validateRequiredStringField(value, result, fieldName, path);

    if (!value.contains(fieldName) || !value.at(fieldName).isString())
    {
        return;
    }

    if (!isValidDuration(value.at(fieldName).asString()))
    {
        result.addError(path + "." + fieldName + " must be an ISO-8601 duration");
    }
}

void validateOptionalDurationField(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    if (!value.contains(fieldName))
    {
        return;
    }

    if (!value.at(fieldName).isString())
    {
        result.addError(path + "." + fieldName + " must be a string");
        return;
    }

    if (!isValidDuration(value.at(fieldName).asString()))
    {
        result.addError(path + "." + fieldName + " must be an ISO-8601 duration");
    }
}

void validateOptionalNonNegativeIntegerField(
    const workflow::json::Value& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    if (!value.contains(fieldName))
    {
        return;
    }

    if (!value.at(fieldName).isInt())
    {
        result.addError(path + "." + fieldName + " must be an integer");
        return;
    }

    if (value.at(fieldName).asInt() < 0)
    {
        result.addError(path + "." + fieldName + " must be greater than or equal to 0");
    }
}

void validateNoAdditionalTopLevelFields(
    const workflow::json::Value& value,
    workflow::ValidationResult& result
)
{
    static const std::set<std::string> allowedFields = {
        kWorkflowName, kWorkflowVersion, kStartWorkflowStepName, kExpectedExecutionTime, kSteps,
    };

    for (const auto& [key, ignored] : value.asObject())
    {
        (void)ignored;

        if (!allowedFields.contains(key))
        {
            result.addError("top-level field '" + key + "' is not allowed");
        }
    }
}

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

ValidationResult validateWorkflowJson(const json::Value& value)
{
    ValidationResult result;

    requireObject(value, result, kRootPath);

    if (!result.valid)
    {
        return result;
    }

    validateNoAdditionalTopLevelFields(value, result);

    validateRequiredNameField(value, result, kWorkflowName, kRootPath);
    validateRequiredStringField(value, result, kStartWorkflowStepName, kRootPath);
    validateRequiredDurationField(value, result, kExpectedExecutionTime, kRootPath);

    requireField(value, result, kWorkflowVersion, kRootPath);

    if (value.contains(kWorkflowVersion))
    {
        if (!value.at(kWorkflowVersion).isInt())
        {
            result.addError("$.workflowVersion must be an integer");
        }
        else if (value.at(kWorkflowVersion).asInt() < 1)
        {
            result.addError("$.workflowVersion must be greater than or equal to 1");
        }
    }

    requireField(value, result, kSteps, kRootPath);

    if (!value.contains(kSteps))
    {
        return result;
    }

    if (!value.at(kSteps).isArray())
    {
        result.addError("$.steps must be an array");
        return result;
    }

    const auto& steps = value.at(kSteps).asArray();

    if (steps.empty())
    {
        result.addError("$.steps must contain at least one step");
        return result;
    }

    std::set<std::string> stepNames;

    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        const auto path = "$.steps[" + std::to_string(i) + "]";
        const auto& step = steps.at(i);

        if (!step.isObject())
        {
            result.addError(path + " must be an object");
            continue;
        }

        validateRequiredNameField(step, result, kName, path);
        validateOptionalDurationField(step, result, kExpectedExecutionTime, path);
        validateOptionalNonNegativeIntegerField(step, result, kMaxRetries, path);

        if (step.contains(kName) && step.at(kName).isString())
        {
            const auto name = step.at(kName).asString();

            if (stepNames.contains(name))
            {
                result.addError(path + ".name duplicates another step name: " + name);
            }
            else
            {
                stepNames.insert(name);
            }
        }
    }

    if (value.contains(kStartWorkflowStepName) && value.at(kStartWorkflowStepName).isString())
    {
        const auto startStepName = value.at(kStartWorkflowStepName).asString();

        if (!stepNames.contains(startStepName))
        {
            result.addError("$.startWorkflowStepName must match one of the workflow step names");
        }
    }

    return result;
}

WorkflowDefinition parseWorkflowDefinition(const json::Value& value)
{
    const auto validation = validateWorkflowJson(value);

    if (!validation.valid)
    {
        std::ostringstream message;
        message << "Invalid workflow definition:";

        for (const auto& error : validation.errors)
        {
            message << "\n- " << error;
        }

        throw std::invalid_argument(message.str());
    }

    WorkflowDefinition workflow;
    workflow.workflowName = value.at(kWorkflowName).asString();
    workflow.workflowVersion = value.at(kWorkflowVersion).asInt();
    workflow.startWorkflowStepName = value.at(kStartWorkflowStepName).asString();
    workflow.expectedExecutionTime = value.at(kExpectedExecutionTime).asString();

    for (const auto& stepValue : value.at(kSteps).asArray())
    {
        WorkflowStep step;
        step.name = stepValue.at(kName).asString();

        if (stepValue.contains(kExpectedExecutionTime))
        {
            step.expectedExecutionTime = stepValue.at(kExpectedExecutionTime).asString();
        }

        if (stepValue.contains(kMaxRetries))
        {
            step.maxRetries = stepValue.at(kMaxRetries).asInt();
        }

        for (const auto& [key, fieldValue] : stepValue.asObject())
        {
            if (key != kName && key != kExpectedExecutionTime && key != kMaxRetries)
            {
                step.additionalFields.emplace(key, fieldValue);
            }
        }

        workflow.steps.push_back(std::move(step));
    }

    return workflow;
}

WorkflowDefinition parseWorkflowDefinitionText(const std::string& jsonText)
{
    try
    {
        const auto value = json::parse(jsonText);
        return parseWorkflowDefinition(value);
    }
    catch (const json::JsonParseError& error)
    {
        throw std::invalid_argument(std::string("Invalid JSON text: ") + error.what());
    }
}

} // namespace workflow
