#pragma once

#include "wf/json.hpp"
#include "wf/store/workflow_definition_store.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_parser.hpp"
#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <string>

namespace workflow::http
{

std::string toIso8601(std::chrono::system_clock::time_point tp);
std::chrono::system_clock::time_point fromIso8601(const std::string& s);

std::string toString(WorkflowExecutionStatus status);
std::string toString(StepExecutionStatus status);

json::Value toJson(const WorkflowStep& step);
json::Value toJson(const WorkflowDefinition& def);
json::Value toJson(const WorkflowDefinitionKey& key);
json::Value toJson(const WorkflowExecution& exec);
json::Value toJson(const WorkflowStepExecution& step);
json::Value toJson(const ValidationResult& result);

} // namespace workflow::http
