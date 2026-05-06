#pragma once

#include "wf/json.hpp"
#include "wf/store/workflow_definition_store.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace workflow
{

struct ValidationResult
{
    bool valid = true;
    std::vector<std::string> errors;

    void addError(const std::string& error)
    {
        valid = false;
        errors.push_back(error);
    }
};

// Timestamp conversion

std::string toIso8601(std::chrono::system_clock::time_point tp);
std::chrono::system_clock::time_point fromIso8601(const std::string& s);

// Enum serialization

std::string toString(WorkflowExecutionStatus status);
std::string toString(StepExecutionStatus status);

// Domain type serialization

json::Value toJson(const WorkflowStep& step);
json::Value toJson(const WorkflowDefinition& def);
json::Value toJson(const WorkflowDefinitionKey& key);
json::Value toJson(const WorkflowExecution& exec);
json::Value toJson(const WorkflowStepExecution& step);
json::Value toJson(const ValidationResult& result);

// Workflow definition parsing and validation

ValidationResult validateWorkflowJson(const json::Value& value);

WorkflowDefinition parseWorkflowDefinition(const json::Value& value);

WorkflowDefinition parseWorkflowDefinitionText(const std::string& jsonText);

} // namespace workflow
