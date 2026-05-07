#pragma once

#include "mt/json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace workflow
{

struct WorkflowStep
{
    std::string name;
    std::optional<std::string> expectedExecutionTime;
    std::optional<int> maxRetries;

    mt::Json::Object additionalFields;
};

struct WorkflowDefinition
{
    std::string workflowName;
    int workflowVersion;
    std::string startWorkflowStepName;
    std::string expectedExecutionTime;
    bool singleton = false;
    std::vector<WorkflowStep> steps;
};

struct WorkflowDefinitionKey
{
    std::string workflowName;
    int workflowVersion;
};

} // namespace workflow
