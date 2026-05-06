#include "wf/logic/step_output_routing_logic.hpp"

namespace workflow::logic
{

NextStepDecision StepOutputRoutingLogic::decideNextStep(const StepCompletionContext& context)
{
    if (context.stepOutput.is_object() && context.stepOutput.as_object().count("nextStep") &&
        context.stepOutput.at("nextStep").is_string() &&
        !context.stepOutput.at("nextStep").as_string().empty())
    {
        mt::Json::Object _obj = context.stepOutput.as_object();
        _obj.erase("nextStep");
        mt::Json nextInput(std::move(_obj));
        return NextStepDecision{
            .workflowComplete = false,
            .nextStepName = context.stepOutput.at("nextStep").as_string(),
            .nextStepInput = std::move(nextInput),
        };
    }

    return NextStepDecision{.workflowComplete = true};
}

} // namespace workflow::logic
