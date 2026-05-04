#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"

#include <stdexcept>

namespace workflow::backend::memory {

void InMemoryWorkflowStepExecutionStore::save(
    const WorkflowStepExecution& stepExecution
) {
    validateStepExecution(stepExecution);

    std::lock_guard<std::mutex> lock(mutex_);

    stepExecutions_[makeKey(stepExecution)] = stepExecution;
}

std::optional<WorkflowStepExecution> InMemoryWorkflowStepExecutionStore::find(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) const {
    validateIdentity(workflowExecutionId, stepName, attempt);

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iter = stepExecutions_.find(makeKey(workflowExecutionId, stepName, attempt));

    if (iter == stepExecutions_.end()) {
        return std::nullopt;
    }

    return iter->second;
}

std::vector<WorkflowStepExecution> InMemoryWorkflowStepExecutionStore::pollAndClaim(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults
) {
    validatePollAndClaimRequest(workflowName, workflowVersion, workerId, maxResults);

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<WorkflowStepExecution> claimed;
    claimed.reserve(maxResults);

    for (auto& [key, stepExecution] : stepExecutions_) {
        (void)key;

        if (claimed.size() >= maxResults) {
            break;
        }

        if (stepExecution.workflowName != workflowName) {
            continue;
        }

        if (stepExecution.workflowVersion != workflowVersion) {
            continue;
        }

        if (stepExecution.status != StepExecutionStatus::Pending) {
            continue;
        }

        stepExecution.status = StepExecutionStatus::Claimed;
        stepExecution.workerId = workerId;

        claimed.push_back(stepExecution);
    }

    return claimed;
}

void InMemoryWorkflowStepExecutionStore::update(
    const WorkflowStepExecution& stepExecution
) {
    validateStepExecution(stepExecution);

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iter = stepExecutions_.find(makeKey(stepExecution));

    if (iter == stepExecutions_.end()) {
        throw std::runtime_error(
            "cannot update missing workflow step execution: " +
            stepExecution.workflowExecutionId + "/" + stepExecution.stepName
        );
    }

    iter->second = stepExecution;
}

void InMemoryWorkflowStepExecutionStore::remove(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) {
    validateIdentity(workflowExecutionId, stepName, attempt);

    std::lock_guard<std::mutex> lock(mutex_);

    stepExecutions_.erase(makeKey(workflowExecutionId, stepName, attempt));
}

void InMemoryWorkflowStepExecutionStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    stepExecutions_.clear();
}

std::size_t InMemoryWorkflowStepExecutionStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return stepExecutions_.size();
}

InMemoryWorkflowStepExecutionStore::Key InMemoryWorkflowStepExecutionStore::makeKey(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) {
    return Key{workflowExecutionId, stepName, attempt};
}

InMemoryWorkflowStepExecutionStore::Key InMemoryWorkflowStepExecutionStore::makeKey(
    const WorkflowStepExecution& stepExecution
) {
    return makeKey(
        stepExecution.workflowExecutionId,
        stepExecution.stepName,
        stepExecution.attempt
    );
}

void InMemoryWorkflowStepExecutionStore::validateIdentity(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) {
    if (workflowExecutionId.empty()) {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }

    if (stepName.empty()) {
        throw std::invalid_argument("stepName must not be empty");
    }

    if (attempt < 0) {
        throw std::invalid_argument("attempt must be greater than or equal to 0");
    }
}

void InMemoryWorkflowStepExecutionStore::validateStepExecution(
    const WorkflowStepExecution& stepExecution
) {
    validateIdentity(
        stepExecution.workflowExecutionId,
        stepExecution.stepName,
        stepExecution.attempt
    );

    if (stepExecution.workflowName.empty()) {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (stepExecution.workflowVersion < 1) {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }
}

void InMemoryWorkflowStepExecutionStore::validatePollAndClaimRequest(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults
) {
    if (workflowName.empty()) {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (workflowVersion < 1) {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }

    if (workerId.empty()) {
        throw std::invalid_argument("workerId must not be empty");
    }

    if (maxResults == 0) {
        throw std::invalid_argument("maxResults must be greater than 0");
    }
}

} // namespace workflow::backend::memory
