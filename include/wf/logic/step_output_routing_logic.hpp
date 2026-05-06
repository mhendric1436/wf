#pragma once

#include "wf/workflow_logic.hpp"

namespace workflow::logic
{

// Routes to the step named by stepOutput["nextStep"]. If that field is absent
// or empty the workflow is marked complete. The orchestrator enforces that any
// returned nextStep name exists in the workflow definition.
class StepOutputRoutingLogic final : public WorkflowLogic
{
  public:
    NextStepDecision decideNextStep(const StepCompletionContext& context) override;
};

} // namespace workflow::logic
