#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"

#include <stdexcept>

namespace workflow::backend::memory
{

std::string InMemoryWorkflowExecutionStore::generateExecutionId()
{
    return "wfexec-" + std::to_string(++nextId_);
}

void InMemoryWorkflowExecutionStore::save(const WorkflowExecution& execution)
{
    validateExecutionId(execution.workflowExecutionId);

    std::lock_guard<std::mutex> lock(mutex_);
    executions_[execution.workflowExecutionId] = execution;
}

std::optional<WorkflowExecution>
InMemoryWorkflowExecutionStore::find(const std::string& workflowExecutionId) const
{
    validateExecutionId(workflowExecutionId);

    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = executions_.find(workflowExecutionId);

    if (iter == executions_.end())
    {
        return std::nullopt;
    }

    return iter->second;
}

void InMemoryWorkflowExecutionStore::update(const WorkflowExecution& execution)
{
    validateExecutionId(execution.workflowExecutionId);

    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = executions_.find(execution.workflowExecutionId);

    if (iter == executions_.end())
    {
        throw std::runtime_error(
            "cannot update missing workflow execution: " + execution.workflowExecutionId
        );
    }

    iter->second = execution;
}

void InMemoryWorkflowExecutionStore::remove(const std::string& workflowExecutionId)
{
    validateExecutionId(workflowExecutionId);

    std::lock_guard<std::mutex> lock(mutex_);
    executions_.erase(workflowExecutionId);
}

void InMemoryWorkflowExecutionStore::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    executions_.clear();
}

std::size_t InMemoryWorkflowExecutionStore::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return executions_.size();
}

void InMemoryWorkflowExecutionStore::validateExecutionId(const std::string& workflowExecutionId)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }
}

} // namespace workflow::backend::memory
