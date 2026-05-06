#pragma once

#include "wf/json.hpp"
#include "wf/workflow_client.hpp"
#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace workflow
{

class WorkflowWorker
{
  public:
    // Handler receives the claimed step and returns the step output.
    // Include {"nextStep": "<name>"} in the output to route to another step.
    // Throw to fail the step.
    using StepHandler = std::function<json::Value(const WorkflowStepExecution&)>;

    struct Options
    {
        // How long to wait between poll attempts when no steps are available.
        std::chrono::milliseconds pollInterval{500};
        // How often to send keep-alive pings while a step is executing.
        std::chrono::milliseconds keepAliveInterval{5000};
    };

    WorkflowWorker(
        WorkflowClient& client,
        std::string workflowName,
        int workflowVersion,
        std::string workerId
    );

    WorkflowWorker(
        WorkflowClient& client,
        std::string workflowName,
        int workflowVersion,
        std::string workerId,
        Options options
    );

    ~WorkflowWorker();

    WorkflowWorker(const WorkflowWorker&) = delete;
    WorkflowWorker& operator=(const WorkflowWorker&) = delete;

    // Register a handler for a named step. Must be called before start().
    void registerStep(
        const std::string& stepName,
        StepHandler handler
    );

    // Start the background poll/execute loop.
    void start();

    // Signal the loop to stop and block until the current step finishes.
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace workflow
