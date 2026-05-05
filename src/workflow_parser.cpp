#include "wf/workflow_parser.hpp"

#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace workflow
{
namespace
{

constexpr const char* FIELD_WORKFLOW_NAME = "workflowName";
constexpr const char* FIELD_WORKFLOW_VERSION = "workflowVersion";
constexpr const char* FIELD_START_WORKFLOW_STEP_NAME = "startWorkflowStepName";
constexpr const char* FIELD_EXPECTED_EXECUTION_TIME = "expectedExecutionTime";
constexpr const char* FIELD_STEPS = "steps";
constexpr const char* FIELD_NAME = "name";
constexpr const char* FIELD_MAX_RETRIES = "maxRetries";
constexpr const char* ROOT_PATH = "$";
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

bool isNonEmptyString(const json::Value& value)
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
    const json::Value& value,
    ValidationResult& result,
    const std::string& path
)
{
    if (!value.isObject())
    {
        result.addError(path + " must be an object");
    }
}

void requireField(
    const json::Value& value,
    ValidationResult& result,
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
    const json::Value& value,
    ValidationResult& result,
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
    const json::Value& value,
    ValidationResult& result,
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
    const json::Value& value,
    ValidationResult& result,
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
    const json::Value& value,
    ValidationResult& result,
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
    const json::Value& value,
    ValidationResult& result,
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
    const json::Value& value,
    ValidationResult& result
)
{
    static const std::set<std::string> allowedFields = {
        FIELD_WORKFLOW_NAME,
        FIELD_WORKFLOW_VERSION,
        FIELD_START_WORKFLOW_STEP_NAME,
        FIELD_EXPECTED_EXECUTION_TIME,
        FIELD_STEPS,
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

ValidationResult validateWorkflowJson(const json::Value& value)
{
    ValidationResult result;

    requireObject(value, result, ROOT_PATH);

    if (!result.valid)
    {
        return result;
    }

    validateNoAdditionalTopLevelFields(value, result);

    validateRequiredNameField(value, result, FIELD_WORKFLOW_NAME, ROOT_PATH);
    validateRequiredStringField(value, result, FIELD_START_WORKFLOW_STEP_NAME, ROOT_PATH);
    validateRequiredDurationField(value, result, FIELD_EXPECTED_EXECUTION_TIME, ROOT_PATH);

    requireField(value, result, FIELD_WORKFLOW_VERSION, ROOT_PATH);

    if (value.contains(FIELD_WORKFLOW_VERSION))
    {
        if (!value.at(FIELD_WORKFLOW_VERSION).isInt())
        {
            result.addError("$.workflowVersion must be an integer");
        }
        else if (value.at(FIELD_WORKFLOW_VERSION).asInt() < 1)
        {
            result.addError("$.workflowVersion must be greater than or equal to 1");
        }
    }

    requireField(value, result, FIELD_STEPS, ROOT_PATH);

    if (!value.contains(FIELD_STEPS))
    {
        return result;
    }

    if (!value.at(FIELD_STEPS).isArray())
    {
        result.addError("$.steps must be an array");
        return result;
    }

    const auto& steps = value.at(FIELD_STEPS).asArray();

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

        validateRequiredNameField(step, result, FIELD_NAME, path);
        validateOptionalDurationField(step, result, FIELD_EXPECTED_EXECUTION_TIME, path);
        validateOptionalNonNegativeIntegerField(step, result, FIELD_MAX_RETRIES, path);

        if (step.contains(FIELD_NAME) && step.at(FIELD_NAME).isString())
        {
            const auto name = step.at(FIELD_NAME).asString();

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

    if (value.contains(FIELD_START_WORKFLOW_STEP_NAME) &&
        value.at(FIELD_START_WORKFLOW_STEP_NAME).isString())
    {
        const auto startStepName = value.at(FIELD_START_WORKFLOW_STEP_NAME).asString();

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
    workflow.workflowName = value.at(FIELD_WORKFLOW_NAME).asString();
    workflow.workflowVersion = value.at(FIELD_WORKFLOW_VERSION).asInt();
    workflow.startWorkflowStepName = value.at(FIELD_START_WORKFLOW_STEP_NAME).asString();
    workflow.expectedExecutionTime = value.at(FIELD_EXPECTED_EXECUTION_TIME).asString();

    for (const auto& stepValue : value.at(FIELD_STEPS).asArray())
    {
        WorkflowStep step;
        step.name = stepValue.at(FIELD_NAME).asString();

        if (stepValue.contains(FIELD_EXPECTED_EXECUTION_TIME))
        {
            step.expectedExecutionTime = stepValue.at(FIELD_EXPECTED_EXECUTION_TIME).asString();
        }

        if (stepValue.contains(FIELD_MAX_RETRIES))
        {
            step.maxRetries = stepValue.at(FIELD_MAX_RETRIES).asInt();
        }

        for (const auto& [key, fieldValue] : stepValue.asObject())
        {
            if (key != FIELD_NAME && key != FIELD_EXPECTED_EXECUTION_TIME &&
                key != FIELD_MAX_RETRIES)
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
