#pragma once

#include "workflow/json.hpp"
#include "workflow/workflow_definition.hpp"

#include <string>
#include <vector>

namespace workflow {

struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;

    void addError(const std::string& error) {
        valid = false;
        errors.push_back(error);
    }
};

ValidationResult validateWorkflowJson(const json::Value& value);

WorkflowDefinition parseWorkflowDefinition(const json::Value& value);

WorkflowDefinition parseWorkflowDefinitionText(const std::string& jsonText);

} // namespace workflow
