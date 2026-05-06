#include "wf/workflow_json.hpp"

#include "mt/errors.hpp"
#include "mt/json_parser.hpp"

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

bool isNonEmptyString(const mt::Json& value)
{
    return value.is_string() && !value.as_string().empty();
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
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& path
)
{
    if (!value.is_object())
    {
        result.addError(path + " must be an object");
    }
}

void requireField(
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    if (!value.is_object() || !value.as_object().count(fieldName))
    {
        result.addError(path + "." + fieldName + " is required");
    }
}

void validateRequiredStringField(
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    requireField(value, result, fieldName, path);

    if (!value.is_object() || !value.as_object().count(fieldName))
    {
        return;
    }

    if (!isNonEmptyString(value.at(fieldName)))
    {
        result.addError(path + "." + fieldName + " must be a non-empty string");
    }
}

void validateRequiredNameField(
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    validateRequiredStringField(value, result, fieldName, path);

    if (!value.is_object() || !value.as_object().count(fieldName) ||
        !value.at(fieldName).is_string())
    {
        return;
    }

    const auto name = value.at(fieldName).as_string();

    if (!isValidName(name))
    {
        result.addError(path + "." + fieldName + " must match pattern " + NAME_PATTERN_STRING);
    }
}

void validateRequiredDurationField(
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    validateRequiredStringField(value, result, fieldName, path);

    if (!value.is_object() || !value.as_object().count(fieldName) ||
        !value.at(fieldName).is_string())
    {
        return;
    }

    if (!isValidDuration(value.at(fieldName).as_string()))
    {
        result.addError(path + "." + fieldName + " must be an ISO-8601 duration");
    }
}

void validateOptionalDurationField(
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    if (!value.is_object() || !value.as_object().count(fieldName))
    {
        return;
    }

    if (!value.at(fieldName).is_string())
    {
        result.addError(path + "." + fieldName + " must be a string");
        return;
    }

    if (!isValidDuration(value.at(fieldName).as_string()))
    {
        result.addError(path + "." + fieldName + " must be an ISO-8601 duration");
    }
}

void validateOptionalNonNegativeIntegerField(
    const mt::Json& value,
    workflow::ValidationResult& result,
    const std::string& fieldName,
    const std::string& path
)
{
    if (!value.is_object() || !value.as_object().count(fieldName))
    {
        return;
    }

    if (!value.at(fieldName).is_int64())
    {
        result.addError(path + "." + fieldName + " must be an integer");
        return;
    }

    if (static_cast<int>(value.at(fieldName).as_int64()) < 0)
    {
        result.addError(path + "." + fieldName + " must be greater than or equal to 0");
    }
}

void validateNoAdditionalTopLevelFields(
    const mt::Json& value,
    workflow::ValidationResult& result
)
{
    static const std::set<std::string> allowedFields = {
        kWorkflowName, kWorkflowVersion, kStartWorkflowStepName, kExpectedExecutionTime, kSteps,
    };

    for (const auto& [key, ignored] : value.as_object())
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
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()) % 1000000;

    std::tm tm{};
    gmtime_r(&tt, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    char result[48];
    std::snprintf(result, sizeof(result), "%s.%06ldZ", buf, static_cast<long>(us.count()));
    return result;
}

std::chrono::system_clock::time_point fromIso8601(const std::string& s)
{
    std::tm tm{};
    char fraction[10] = {};

    std::sscanf(
        s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%9[0-9]Z", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec, fraction
    );

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = 0;

    std::string fractionalPart = fraction;
    if (fractionalPart.size() > 6)
    {
        fractionalPart.resize(6);
    }
    while (fractionalPart.size() < 6)
    {
        fractionalPart.push_back('0');
    }

    const int us = fractionalPart.empty() ? 0 : std::stoi(fractionalPart);
    const auto tt = timegm(&tm);
    return std::chrono::system_clock::from_time_t(tt) + std::chrono::microseconds(us);
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

mt::Json toJson(const WorkflowStep& step)
{
    mt::Json::Object obj;
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

    return mt::Json(std::move(obj));
}

mt::Json toJson(const WorkflowDefinition& def)
{
    mt::Json::Array steps;
    for (const auto& step : def.steps)
    {
        steps.push_back(toJson(step));
    }

    mt::Json::Object obj;
    obj[kWorkflowName] = def.workflowName;
    obj[kWorkflowVersion] = def.workflowVersion;
    obj[kStartWorkflowStepName] = def.startWorkflowStepName;
    obj[kExpectedExecutionTime] = def.expectedExecutionTime;
    obj[kSteps] = mt::Json(std::move(steps));
    return mt::Json(std::move(obj));
}

mt::Json toJson(const WorkflowDefinitionKey& key)
{
    mt::Json::Object obj;
    obj[kWorkflowName] = key.workflowName;
    obj[kWorkflowVersion] = key.workflowVersion;
    return mt::Json(std::move(obj));
}

mt::Json toJson(const WorkflowExecution& exec)
{
    mt::Json::Object obj;
    obj[kWorkflowExecutionId] = exec.workflowExecutionId;
    obj[kWorkflowName] = exec.workflowName;
    obj[kWorkflowVersion] = exec.workflowVersion;
    obj[kStatus] = toString(exec.status);
    obj[kCurrentStepName] = exec.currentStepName;
    obj[kInput] = exec.input;
    obj[kState] = exec.state;
    obj[kCurrentStepAttempt] = exec.currentStepAttempt;
    obj[kFailureReason] =
        exec.failureReason.has_value() ? mt::Json(exec.failureReason.value()) : mt::Json{};
    obj[kStartedAt] =
        exec.startedAt.has_value() ? mt::Json(toIso8601(exec.startedAt.value())) : mt::Json{};
    obj[kCompletedAt] =
        exec.completedAt.has_value() ? mt::Json(toIso8601(exec.completedAt.value())) : mt::Json{};
    return mt::Json(std::move(obj));
}

mt::Json toJson(const WorkflowStepExecution& step)
{
    mt::Json::Object obj;
    obj[kWorkflowExecutionId] = step.workflowExecutionId;
    obj[kWorkflowName] = step.workflowName;
    obj[kWorkflowVersion] = step.workflowVersion;
    obj[kStepName] = step.stepName;
    obj[kAttempt] = step.attempt;
    obj[kStatus] = toString(step.status);
    obj[kWorkerId] = step.workerId.has_value() ? mt::Json(step.workerId.value()) : mt::Json{};
    obj[kLeaseExpiresAt] = step.leaseExpiresAt.has_value()
                               ? mt::Json(toIso8601(step.leaseExpiresAt.value()))
                               : mt::Json{};
    obj[kFailureReason] =
        step.failureReason.has_value() ? mt::Json(step.failureReason.value()) : mt::Json{};
    obj[kCreatedAt] =
        step.createdAt.has_value() ? mt::Json(toIso8601(step.createdAt.value())) : mt::Json{};
    obj[kStartedAt] =
        step.startedAt.has_value() ? mt::Json(toIso8601(step.startedAt.value())) : mt::Json{};
    obj[kCompletedAt] =
        step.completedAt.has_value() ? mt::Json(toIso8601(step.completedAt.value())) : mt::Json{};
    obj[kInput] = step.input;
    obj[kState] = step.state;
    obj[kOutput] = step.output;
    return mt::Json(std::move(obj));
}

WorkflowExecutionStatus executionStatusFromString(const std::string& s)
{
    if (s == "Completed")
    {
        return WorkflowExecutionStatus::Completed;
    }
    if (s == "Failed")
    {
        return WorkflowExecutionStatus::Failed;
    }
    if (s == "Canceled")
    {
        return WorkflowExecutionStatus::Canceled;
    }
    return WorkflowExecutionStatus::Running;
}

StepExecutionStatus stepStatusFromString(const std::string& s)
{
    if (s == "Running")
    {
        return StepExecutionStatus::Running;
    }
    if (s == "Completed")
    {
        return StepExecutionStatus::Completed;
    }
    if (s == "Failed")
    {
        return StepExecutionStatus::Failed;
    }
    if (s == "Canceled")
    {
        return StepExecutionStatus::Canceled;
    }
    return StepExecutionStatus::Pending;
}

WorkflowDefinitionKey workflowDefinitionKeyFromJson(const mt::Json& v)
{
    return WorkflowDefinitionKey{
        .workflowName = v.at(kWorkflowName).as_string(),
        .workflowVersion = static_cast<int>(v.at(kWorkflowVersion).as_int64()),
    };
}

WorkflowExecution workflowExecutionFromJson(const mt::Json& v)
{
    WorkflowExecution exec;
    exec.workflowExecutionId = v.at(kWorkflowExecutionId).as_string();
    exec.workflowName = v.at(kWorkflowName).as_string();
    exec.workflowVersion = static_cast<int>(v.at(kWorkflowVersion).as_int64());
    exec.status = executionStatusFromString(v.at(kStatus).as_string());
    exec.currentStepName = v.at(kCurrentStepName).as_string();
    exec.input = v.at(kInput);
    exec.state = v.at(kState);
    exec.currentStepAttempt = static_cast<int>(v.at(kCurrentStepAttempt).as_int64());

    if (v.is_object() && v.as_object().count(kFailureReason) && !v.at(kFailureReason).is_null())
    {
        exec.failureReason = v.at(kFailureReason).as_string();
    }

    if (v.is_object() && v.as_object().count(kStartedAt) && !v.at(kStartedAt).is_null())
    {
        exec.startedAt = fromIso8601(v.at(kStartedAt).as_string());
    }

    if (v.is_object() && v.as_object().count(kCompletedAt) && !v.at(kCompletedAt).is_null())
    {
        exec.completedAt = fromIso8601(v.at(kCompletedAt).as_string());
    }

    return exec;
}

WorkflowStepExecution workflowStepExecutionFromJson(const mt::Json& v)
{
    WorkflowStepExecution step;
    step.workflowExecutionId = v.at(kWorkflowExecutionId).as_string();
    step.workflowName = v.at(kWorkflowName).as_string();
    step.workflowVersion = static_cast<int>(v.at(kWorkflowVersion).as_int64());
    step.stepName = v.at(kStepName).as_string();
    step.attempt = static_cast<int>(v.at(kAttempt).as_int64());
    step.status = stepStatusFromString(v.at(kStatus).as_string());

    if (v.is_object() && v.as_object().count(kWorkerId) && !v.at(kWorkerId).is_null())
    {
        step.workerId = v.at(kWorkerId).as_string();
    }

    if (v.is_object() && v.as_object().count(kLeaseExpiresAt) && !v.at(kLeaseExpiresAt).is_null())
    {
        step.leaseExpiresAt = fromIso8601(v.at(kLeaseExpiresAt).as_string());
    }

    if (v.is_object() && v.as_object().count(kFailureReason) && !v.at(kFailureReason).is_null())
    {
        step.failureReason = v.at(kFailureReason).as_string();
    }

    if (v.is_object() && v.as_object().count(kCreatedAt) && !v.at(kCreatedAt).is_null())
    {
        step.createdAt = fromIso8601(v.at(kCreatedAt).as_string());
    }

    if (v.is_object() && v.as_object().count(kStartedAt) && !v.at(kStartedAt).is_null())
    {
        step.startedAt = fromIso8601(v.at(kStartedAt).as_string());
    }

    if (v.is_object() && v.as_object().count(kCompletedAt) && !v.at(kCompletedAt).is_null())
    {
        step.completedAt = fromIso8601(v.at(kCompletedAt).as_string());
    }

    if (v.is_object() && v.as_object().count(kInput))
    {
        step.input = v.at(kInput);
    }

    if (v.is_object() && v.as_object().count(kState))
    {
        step.state = v.at(kState);
    }

    if (v.is_object() && v.as_object().count(kOutput))
    {
        step.output = v.at(kOutput);
    }

    return step;
}

ValidationResult validationResultFromJson(const mt::Json& v)
{
    ValidationResult result;
    result.valid = v.at(kValid).as_bool();

    for (const auto& e : v.at(kErrors).as_array())
    {
        result.errors.push_back(e.as_string());
    }

    return result;
}

mt::Json toJson(const ValidationResult& result)
{
    mt::Json::Array errors;
    for (const auto& e : result.errors)
    {
        errors.push_back(mt::Json(e));
    }

    mt::Json::Object obj;
    obj[kValid] = result.valid;
    obj[kErrors] = mt::Json(std::move(errors));
    return mt::Json(std::move(obj));
}

ValidationResult validateWorkflowJson(const mt::Json& value)
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

    if (value.is_object() && value.as_object().count(kWorkflowVersion))
    {
        if (!value.at(kWorkflowVersion).is_int64())
        {
            result.addError("$.workflowVersion must be an integer");
        }
        else if (static_cast<int>(value.at(kWorkflowVersion).as_int64()) < 1)
        {
            result.addError("$.workflowVersion must be greater than or equal to 1");
        }
    }

    requireField(value, result, kSteps, kRootPath);

    if (!value.is_object() || !value.as_object().count(kSteps))
    {
        return result;
    }

    if (!value.at(kSteps).is_array())
    {
        result.addError("$.steps must be an array");
        return result;
    }

    const auto& steps = value.at(kSteps).as_array();

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

        if (!step.is_object())
        {
            result.addError(path + " must be an object");
            continue;
        }

        validateRequiredNameField(step, result, kName, path);
        validateOptionalDurationField(step, result, kExpectedExecutionTime, path);
        validateOptionalNonNegativeIntegerField(step, result, kMaxRetries, path);

        if (step.is_object() && step.as_object().count(kName) && step.at(kName).is_string())
        {
            const auto name = step.at(kName).as_string();

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

    if (value.is_object() && value.as_object().count(kStartWorkflowStepName) &&
        value.at(kStartWorkflowStepName).is_string())
    {
        const auto startStepName = value.at(kStartWorkflowStepName).as_string();

        if (!stepNames.contains(startStepName))
        {
            result.addError("$.startWorkflowStepName must match one of the workflow step names");
        }
    }

    return result;
}

WorkflowDefinition parseWorkflowDefinition(const mt::Json& value)
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
    workflow.workflowName = value.at(kWorkflowName).as_string();
    workflow.workflowVersion = static_cast<int>(value.at(kWorkflowVersion).as_int64());
    workflow.startWorkflowStepName = value.at(kStartWorkflowStepName).as_string();
    workflow.expectedExecutionTime = value.at(kExpectedExecutionTime).as_string();

    for (const auto& stepValue : value.at(kSteps).as_array())
    {
        WorkflowStep step;
        step.name = stepValue.at(kName).as_string();

        if (stepValue.is_object() && stepValue.as_object().count(kExpectedExecutionTime))
        {
            step.expectedExecutionTime = stepValue.at(kExpectedExecutionTime).as_string();
        }

        if (stepValue.is_object() && stepValue.as_object().count(kMaxRetries))
        {
            step.maxRetries = static_cast<int>(stepValue.at(kMaxRetries).as_int64());
        }

        for (const auto& [key, fieldValue] : stepValue.as_object())
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
        const auto value = mt::JsonParser(jsonText).parse();
        return parseWorkflowDefinition(value);
    }
    catch (const mt::BackendError& error)
    {
        throw std::invalid_argument(std::string("Invalid JSON text: ") + error.what());
    }
}

} // namespace workflow
