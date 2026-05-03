#pragma once

#include "wf/json.hpp"

#include <optional>
#include <string>

namespace workflow {

struct StepCompletionContext {
    std::string workflowName;
    int workflowVersion;
    std::string workflowExecutionId;
    std::string completedStepName;
    json::Value input;
    json::Value state;
    json::Value stepOutput;
};

struct NextStepDecision {
    bool workflowComplete = false;
    std::optional<std::string> nextStepName;
    json::Value updatedState = json::Value::object();
};

class WorkflowLogic {
  public:
    virtual ~WorkflowLogic() = default;

    virtual NextStepDecision decideNextStep(
        const StepCompletionContext& context
    ) = 0;
};

} // namespace workflow
