#pragma once

#include "mt/json.hpp"

#include <optional>
#include <string>

namespace workflow
{

struct StepCompletionContext
{
    std::string workflowName;
    int workflowVersion;
    std::string workflowExecutionId;
    std::string completedStepName;
    mt::Json input;
    mt::Json state;
    mt::Json stepOutput;
};

struct NextStepDecision
{
    bool workflowComplete = false;
    std::optional<std::string> nextStepName;
    mt::Json updatedState = mt::Json(mt::Json::Object{});
    mt::Json nextStepInput = mt::Json(mt::Json::Object{});
};

class WorkflowLogic
{
  public:
    virtual ~WorkflowLogic() = default;

    virtual NextStepDecision decideNextStep(const StepCompletionContext& context) = 0;
};

} // namespace workflow
