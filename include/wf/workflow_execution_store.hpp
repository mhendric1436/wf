#pragma once

#include "wf/workflow_execution.hpp"

#include <optional>
#include <string>

namespace workflow {

class WorkflowExecutionStore {
  public:
    virtual ~WorkflowExecutionStore() = default;

    virtual void save(const WorkflowExecution& execution) = 0;

    virtual std::optional<WorkflowExecution> find(
        const std::string& workflowExecutionId
    ) const = 0;

    virtual void update(const WorkflowExecution& execution) = 0;
};

} // namespace workflow
