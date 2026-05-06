#pragma once

#include "wf/store/workflow_definition_store.hpp"
#include "wf/workflow_logic.hpp"

namespace workflow::logic
{

// Advances through steps in the order they appear in the workflow definition.
// Marks the workflow complete after the last step.
class SequentialWorkflowLogic final : public WorkflowLogic
{
  public:
    explicit SequentialWorkflowLogic(WorkflowDefinitionStore& store);

    NextStepDecision decideNextStep(const StepCompletionContext& context) override;

  private:
    WorkflowDefinitionStore& store_;
};

} // namespace workflow::logic
