#include "wf/logic/step_output_routing_logic.hpp"

namespace workflow::logic
{

NextStepDecision StepOutputRoutingLogic::decideNextStep(const StepCompletionContext& context)
{
    if (context.stepOutput.contains("nextStep") && context.stepOutput.at("nextStep").isString() &&
        !context.stepOutput.at("nextStep").asString().empty())
    {
        return NextStepDecision{
            .workflowComplete = false,
            .nextStepName = context.stepOutput.at("nextStep").asString(),
        };
    }

    return NextStepDecision{.workflowComplete = true};
}

} // namespace workflow::logic
