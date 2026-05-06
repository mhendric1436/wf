#include "wf/logic/sequential_workflow_logic.hpp"

namespace workflow::logic
{

SequentialWorkflowLogic::SequentialWorkflowLogic(WorkflowDefinitionStore& store)
    : store_(store)
{
}

NextStepDecision SequentialWorkflowLogic::decideNextStep(const StepCompletionContext& context)
{
    const auto def = store_.find(context.workflowName, context.workflowVersion);

    if (!def.has_value())
    {
        return NextStepDecision{.workflowComplete = true};
    }

    const auto& steps = def->steps;

    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].name == context.completedStepName)
        {
            if (i + 1 < steps.size())
            {
                return NextStepDecision{
                    .workflowComplete = false,
                    .nextStepName = steps[i + 1].name,
                };
            }
            return NextStepDecision{.workflowComplete = true};
        }
    }

    return NextStepDecision{.workflowComplete = true};
}

} // namespace workflow::logic
