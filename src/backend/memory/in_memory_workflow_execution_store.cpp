#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"

#include <stdexcept>

namespace workflow::backend::memory
{

void InMemoryWorkflowExecutionStore::save(const WorkflowExecution& execution)
{
    validateExecutionId(execution.workflowExecutionId);

    executions_[execution.workflowExecutionId] = execution;
}

std::optional<WorkflowExecution>
InMemoryWorkflowExecutionStore::find(const std::string& workflowExecutionId) const
{
    validateExecutionId(workflowExecutionId);

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

    executions_.erase(workflowExecutionId);
}

void InMemoryWorkflowExecutionStore::clear()
{
    executions_.clear();
}

std::size_t InMemoryWorkflowExecutionStore::size() const
{
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
