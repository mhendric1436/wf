#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"

#include <stdexcept>

namespace workflow::backend::memory
{

void InMemoryWorkflowStepExecutionStore::save(const WorkflowStepExecution& stepExecution)
{
    validateStepExecution(stepExecution);

    std::lock_guard<std::mutex> lock(mutex_);

    stepExecutions_[makeKey(stepExecution)] = stepExecution;
}

std::optional<WorkflowStepExecution> InMemoryWorkflowStepExecutionStore::find(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) const
{
    validateIdentity(workflowExecutionId, stepName, attempt);

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iter = stepExecutions_.find(makeKey(workflowExecutionId, stepName, attempt));

    if (iter == stepExecutions_.end())
    {
        return std::nullopt;
    }

    return iter->second;
}

std::vector<WorkflowStepExecution> InMemoryWorkflowStepExecutionStore::pollAndClaim(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults,
    const std::map<
        std::string,
        std::chrono::seconds>& leaseDurationsByStepName
)
{
    validatePollAndClaimRequest(
        workflowName, workflowVersion, workerId, maxResults, leaseDurationsByStepName
    );

    const auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<WorkflowStepExecution> result;
    result.reserve(maxResults);

    for (auto& [key, stepExecution] : stepExecutions_)
    {
        (void)key;

        if (result.size() >= maxResults)
        {
            break;
        }

        if (stepExecution.workflowName != workflowName)
        {
            continue;
        }

        if (stepExecution.workflowVersion != workflowVersion)
        {
            continue;
        }

        if (!isClaimable(stepExecution, now))
        {
            continue;
        }

        const auto durationIter = leaseDurationsByStepName.find(stepExecution.stepName);

        if (durationIter == leaseDurationsByStepName.end())
        {
            continue;
        }

        validateLeaseDuration(durationIter->second);

        stepExecution.status = StepExecutionStatus::Running;
        stepExecution.workerId = workerId;
        stepExecution.leaseExpiresAt = now + durationIter->second;
        stepExecution.failureReason.reset();

        result.push_back(stepExecution);
    }

    return result;
}

WorkflowStepExecution InMemoryWorkflowStepExecutionStore::keepAlive(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt,
    const std::string& workerId,
    std::chrono::seconds leaseDuration
)
{
    validateIdentity(workflowExecutionId, stepName, attempt);
    validateWorkerId(workerId);
    validateLeaseDuration(leaseDuration);

    const auto now = std::chrono::system_clock::now();
    const auto newLeaseExpiresAt = now + leaseDuration;

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iter = stepExecutions_.find(makeKey(workflowExecutionId, stepName, attempt));

    if (iter == stepExecutions_.end())
    {
        throw std::runtime_error(
            "workflow step execution not found: " + workflowExecutionId + "/" + stepName
        );
    }

    auto& stepExecution = iter->second;

    if (stepExecution.status != StepExecutionStatus::Running)
    {
        throw std::runtime_error(
            "workflow step execution is not running: " + workflowExecutionId + "/" + stepName
        );
    }

    if (!stepExecution.workerId.has_value() || stepExecution.workerId.value() != workerId)
    {
        throw std::runtime_error(
            "workflow step execution is owned by a different worker: " + workflowExecutionId + "/" +
            stepName
        );
    }

    if (!stepExecution.leaseExpiresAt.has_value())
    {
        throw std::runtime_error(
            "workflow step execution does not have an active lease: " + workflowExecutionId + "/" +
            stepName
        );
    }

    if (stepExecution.leaseExpiresAt.value() <= now)
    {
        throw std::runtime_error(
            "workflow step execution lease has expired: " + workflowExecutionId + "/" + stepName
        );
    }

    stepExecution.leaseExpiresAt = newLeaseExpiresAt;

    return stepExecution;
}

void InMemoryWorkflowStepExecutionStore::update(const WorkflowStepExecution& stepExecution)
{
    validateStepExecution(stepExecution);

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iter = stepExecutions_.find(makeKey(stepExecution));

    if (iter == stepExecutions_.end())
    {
        throw std::runtime_error(
            "cannot update missing workflow step execution: " + stepExecution.workflowExecutionId +
            "/" + stepExecution.stepName
        );
    }

    iter->second = stepExecution;
}

void InMemoryWorkflowStepExecutionStore::cancelByExecution(const std::string& workflowExecutionId)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [key, stepExecution] : stepExecutions_)
    {
        (void)key;

        if (stepExecution.workflowExecutionId != workflowExecutionId)
        {
            continue;
        }

        if (stepExecution.status == StepExecutionStatus::Pending ||
            stepExecution.status == StepExecutionStatus::Running)
        {
            stepExecution.status = StepExecutionStatus::Canceled;
            stepExecution.workerId.reset();
            stepExecution.leaseExpiresAt.reset();
        }
    }
}

void InMemoryWorkflowStepExecutionStore::remove(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    validateIdentity(workflowExecutionId, stepName, attempt);

    std::lock_guard<std::mutex> lock(mutex_);

    stepExecutions_.erase(makeKey(workflowExecutionId, stepName, attempt));
}

void InMemoryWorkflowStepExecutionStore::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);

    stepExecutions_.clear();
}

std::size_t InMemoryWorkflowStepExecutionStore::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return stepExecutions_.size();
}

InMemoryWorkflowStepExecutionStore::Key InMemoryWorkflowStepExecutionStore::makeKey(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    return Key{workflowExecutionId, stepName, attempt};
}

InMemoryWorkflowStepExecutionStore::Key
InMemoryWorkflowStepExecutionStore::makeKey(const WorkflowStepExecution& stepExecution)
{
    return makeKey(
        stepExecution.workflowExecutionId, stepExecution.stepName, stepExecution.attempt
    );
}

void InMemoryWorkflowStepExecutionStore::validateIdentity(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }

    if (stepName.empty())
    {
        throw std::invalid_argument("stepName must not be empty");
    }

    if (attempt < 0)
    {
        throw std::invalid_argument("attempt must be greater than or equal to 0");
    }
}

void InMemoryWorkflowStepExecutionStore::validateStepExecution(
    const WorkflowStepExecution& stepExecution
)
{
    validateIdentity(
        stepExecution.workflowExecutionId, stepExecution.stepName, stepExecution.attempt
    );

    if (stepExecution.workflowName.empty())
    {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (stepExecution.workflowVersion < 1)
    {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }
}

void InMemoryWorkflowStepExecutionStore::validateWorkerId(const std::string& workerId)
{
    if (workerId.empty())
    {
        throw std::invalid_argument("workerId must not be empty");
    }
}

void InMemoryWorkflowStepExecutionStore::validateLeaseDuration(std::chrono::seconds leaseDuration)
{
    if (leaseDuration <= std::chrono::seconds{0})
    {
        throw std::invalid_argument("leaseDuration must be greater than 0 seconds");
    }
}

void InMemoryWorkflowStepExecutionStore::validatePollAndClaimRequest(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults,
    const std::map<
        std::string,
        std::chrono::seconds>& leaseDurationsByStepName
)
{
    if (workflowName.empty())
    {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (workflowVersion < 1)
    {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }

    validateWorkerId(workerId);

    if (maxResults == 0)
    {
        throw std::invalid_argument("maxResults must be greater than 0");
    }

    for (const auto& [stepName, leaseDuration] : leaseDurationsByStepName)
    {
        if (stepName.empty())
        {
            throw std::invalid_argument("lease duration step name must not be empty");
        }

        validateLeaseDuration(leaseDuration);
    }
}

bool InMemoryWorkflowStepExecutionStore::isClaimable(
    const WorkflowStepExecution& stepExecution,
    std::chrono::system_clock::time_point now
)
{
    if (stepExecution.status == StepExecutionStatus::Pending)
    {
        return true;
    }

    if (stepExecution.status != StepExecutionStatus::Running)
    {
        return false;
    }

    return stepExecution.leaseExpiresAt.has_value() && stepExecution.leaseExpiresAt.value() <= now;
}

} // namespace workflow::backend::memory
