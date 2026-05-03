#pragma once

#include "wf/store/workflow_execution_store.hpp"

#include <map>
#include <optional>
#include <string>

namespace workflow::backend::memory
{

class InMemoryWorkflowExecutionStore final : public workflow::WorkflowExecutionStore
{
  public:
    void save(const WorkflowExecution& execution) override;

    std::optional<WorkflowExecution> find(const std::string& workflowExecutionId) const override;

    void update(const WorkflowExecution& execution) override;

    void remove(const std::string& workflowExecutionId);

    void clear();

    std::size_t size() const;

  private:
    static void validateExecutionId(const std::string& workflowExecutionId);

    std::map<std::string, WorkflowExecution> executions_;
};

} // namespace workflow::backend::memory
