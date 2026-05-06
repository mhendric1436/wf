#pragma once

#include "wf/store/workflow_definition_store.hpp"
#include "wf/workflow_client.hpp"
#include "wf/workflow_worker.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace workflow
{

class WorkflowWorkerPool
{
  public:
    using StepHandler = WorkflowWorker::StepHandler;

    struct Options
    {
        // Number of threads that execute step handlers concurrently.
        std::size_t threadCount{4};
        // Number of threads that poll for available steps, each round-robining
        // across all watched definitions.
        std::size_t pollerCount{2};
        // Maximum steps claimed per poll call. The actual request is capped at
        // the number of idle executor threads so steps are never claimed faster
        // than they can be executed.
        std::size_t maxResultsPerPoll{4};
        // How long to wait between poll cycles when no steps are found.
        std::chrono::milliseconds pollInterval{500};
        // How often to send keep-alive pings while a step is executing.
        std::chrono::milliseconds keepAliveInterval{5000};
    };

    WorkflowWorkerPool(
        WorkflowClient& client,
        std::vector<WorkflowDefinitionKey> workflowDefinitions,
        std::string workerId
    );

    WorkflowWorkerPool(
        WorkflowClient& client,
        std::vector<WorkflowDefinitionKey> workflowDefinitions,
        std::string workerId,
        Options options
    );

    ~WorkflowWorkerPool();

    WorkflowWorkerPool(const WorkflowWorkerPool&) = delete;
    WorkflowWorkerPool& operator=(const WorkflowWorkerPool&) = delete;

    // Register a handler for stepName across all watched workflow definitions.
    // Must be called before start().
    void registerStep(
        const std::string& stepName,
        StepHandler handler
    );

    void start();

    // Signal all threads to stop and block until all in-flight steps finish.
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace workflow
