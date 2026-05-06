#pragma once

#include "mt/json.hpp"
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

// Enum serialization / deserialization

std::string toString(WorkflowExecutionStatus status);
std::string toString(StepExecutionStatus status);

WorkflowExecutionStatus executionStatusFromString(const std::string& s);
StepExecutionStatus stepStatusFromString(const std::string& s);

// Domain type serialization

mt::Json toJson(const WorkflowStep& step);
mt::Json toJson(const WorkflowDefinition& def);
mt::Json toJson(const WorkflowDefinitionKey& key);
mt::Json toJson(const WorkflowExecution& exec);
mt::Json toJson(const WorkflowStepExecution& step);
mt::Json toJson(const ValidationResult& result);

// Domain type deserialization

WorkflowDefinitionKey workflowDefinitionKeyFromJson(const mt::Json& v);
WorkflowExecution workflowExecutionFromJson(const mt::Json& v);
WorkflowStepExecution workflowStepExecutionFromJson(const mt::Json& v);
ValidationResult validationResultFromJson(const mt::Json& v);

// Workflow definition parsing and validation

ValidationResult validateWorkflowJson(const mt::Json& value);

WorkflowDefinition parseWorkflowDefinition(const mt::Json& value);

WorkflowDefinition parseWorkflowDefinitionText(const std::string& jsonText);

} // namespace workflow
