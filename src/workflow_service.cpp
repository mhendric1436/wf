#include "wf/workflow_service.hpp"

namespace workflow
{

WorkflowService::WorkflowService(
    WorkflowOrchestrator& orchestrator,
    std::chrono::seconds sweepInterval
)
    : orchestrator_(orchestrator),
      sweepInterval_(sweepInterval),
      sweepThread_([this] { runSweepLoop(); })
{
}

WorkflowService::~WorkflowService()
{
    {
        std::lock_guard<std::mutex> lock(sweepMutex_);
        stopping_ = true;
    }
    sweepCv_.notify_one();
    sweepThread_.join();
}

void WorkflowService::runSweepLoop()
{
    std::unique_lock<std::mutex> lock(sweepMutex_);

    while (!stopping_)
    {
        sweepCv_.wait_for(lock, sweepInterval_, [this] { return stopping_.load(); });

        if (!stopping_)
        {
            lock.unlock();
            orchestrator_.sweepExpiredLeases();
            lock.lock();
        }
    }
}

ValidateWorkflowDefinitionResponse
WorkflowService::validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request) const
{
    return ValidateWorkflowDefinitionResponse{
        .validation = validateWorkflowJson(request.definitionJson),
    };
}

RegisterWorkflowDefinitionResponse
WorkflowService::registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request)
{
    WorkflowDefinition definition = parseWorkflowDefinition(request.definitionJson);

    definition = orchestrator_.registerWorkflowDefinition(definition);

    return RegisterWorkflowDefinitionResponse{
        .definition = definition,
    };
}

StartWorkflowExecutionResponse
WorkflowService::startWorkflowExecution(const StartWorkflowExecutionRequest& request)
{
    return StartWorkflowExecutionResponse{
        .execution = orchestrator_.startWorkflow(
            request.workflowName, request.workflowVersion, request.input
        ),
    };
}

PollAndClaimWorkflowStepsResponse
WorkflowService::pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request)
{
    return PollAndClaimWorkflowStepsResponse{
        .steps = orchestrator_.pollAndClaimWorkflowSteps(
            request.workflowName, request.workflowVersion, request.workerId, request.maxResults
        ),
    };
}

KeepAliveWorkflowStepResponse
WorkflowService::keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request)
{
    return KeepAliveWorkflowStepResponse{
        .step = orchestrator_.keepAliveStep(
            request.workflowExecutionId, request.stepName, request.workerId
        ),
    };
}

CompleteWorkflowStepResponse
WorkflowService::completeWorkflowStep(const CompleteWorkflowStepRequest& request)
{
    return CompleteWorkflowStepResponse{
        .execution = orchestrator_.completeStep(
            request.workflowExecutionId, request.stepName, request.workerId, request.stepOutput
        ),
    };
}

FailWorkflowStepResponse WorkflowService::failWorkflowStep(const FailWorkflowStepRequest& request)
{
    return FailWorkflowStepResponse{
        .execution = orchestrator_.failStep(
            request.workflowExecutionId, request.stepName, request.workerId, request.reason
        ),
    };
}

CancelWorkflowResponse WorkflowService::cancelWorkflow(const CancelWorkflowRequest& request)
{
    return CancelWorkflowResponse{
        .execution = orchestrator_.cancelWorkflow(request.workflowExecutionId),
    };
}

GetWorkflowExecutionResponse
WorkflowService::getWorkflowExecution(const GetWorkflowExecutionRequest& request) const
{
    return GetWorkflowExecutionResponse{
        .execution = orchestrator_.getWorkflowExecution(request.workflowExecutionId),
    };
}

ListWorkflowDefinitionsResponse
WorkflowService::listWorkflowDefinitions(const ListWorkflowDefinitionsRequest&) const
{
    return ListWorkflowDefinitionsResponse{
        .definitions = orchestrator_.listWorkflowDefinitions(),
    };
}

} // namespace workflow
