#include "wf/workflow_worker_pool.hpp"

#include "keep_alive_thread.hpp"
#include "wf/workflow_service.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace workflow
{

struct WorkflowWorkerPool::Impl
{
    WorkflowClient& client;
    std::vector<WorkflowDefinitionKey> definitions;
    std::string workerId;
    Options options;

    std::map<std::string, StepHandler> handlers;

    // Serializes all WorkflowClient calls across pollers and executor keep-alive threads.
    std::mutex clientMutex;

    // Guards workQueue and idleExecutors.
    std::mutex stateMutex;
    std::condition_variable queueNotEmpty;
    // Pollers wait here when idleExecutors == 0; executors notify after each step.
    std::condition_variable slotsAvailable;
    std::queue<WorkflowStepExecution> workQueue;
    int idleExecutors;

    std::atomic<bool> stopping{false};
    std::vector<std::thread> pollerThreads;
    std::vector<std::thread> executorThreads;

    Impl(
        WorkflowClient& client_,
        std::vector<WorkflowDefinitionKey> definitions_,
        std::string workerId_,
        Options options_
    )
        : client(client_),
          definitions(std::move(definitions_)),
          workerId(std::move(workerId_)),
          options(std::move(options_)),
          idleExecutors(static_cast<int>(options.threadCount))
    {
    }

    void start()
    {
        stopping = false;
        idleExecutors = static_cast<int>(options.threadCount);

        for (std::size_t i = 0; i < options.pollerCount; ++i)
        {
            pollerThreads.emplace_back([this, i] { runPoller(i); });
        }

        for (std::size_t i = 0; i < options.threadCount; ++i)
        {
            executorThreads.emplace_back([this] { runExecutor(); });
        }
    }

    void stop()
    {
        if (pollerThreads.empty() && executorThreads.empty())
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            stopping = true;
        }
        slotsAvailable.notify_all();
        queueNotEmpty.notify_all();

        for (auto& t : pollerThreads)
        {
            t.join();
        }
        pollerThreads.clear();

        for (auto& t : executorThreads)
        {
            t.join();
        }
        executorThreads.clear();
    }

    void runPoller(std::size_t pollerIndex)
    {
        if (definitions.empty())
        {
            return;
        }

        const std::size_t n = definitions.size();
        std::size_t defIdx = pollerIndex % n;

        while (!stopping)
        {
            bool foundWork = false;

            for (std::size_t i = 0; i < n && !stopping; ++i)
            {
                const auto& def = definitions[defIdx];
                defIdx = (defIdx + 1) % n;

                // Speculatively claim executor slots before polling so we never
                // claim more steps than we can immediately execute.
                std::size_t toRequest = 0;
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    if (idleExecutors <= 0)
                    {
                        continue;
                    }
                    toRequest = std::min(
                        static_cast<std::size_t>(idleExecutors), options.maxResultsPerPoll
                    );
                    idleExecutors -= static_cast<int>(toRequest);
                }

                PollAndClaimWorkflowStepsResponse resp;
                try
                {
                    std::lock_guard<std::mutex> clientLock(clientMutex);
                    resp = client.pollAndClaimWorkflowSteps(
                        PollAndClaimWorkflowStepsRequest{
                            .workflowName = def.workflowName,
                            .workflowVersion = def.workflowVersion,
                            .workerId = workerId,
                            .maxResults = toRequest,
                        }
                    );
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    idleExecutors += static_cast<int>(toRequest);
                    slotsAvailable.notify_one();
                    continue;
                }

                const std::size_t actual = resp.steps.size();
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    // Return any over-claimed slots.
                    idleExecutors += static_cast<int>(toRequest - actual);
                    for (auto& step : resp.steps)
                    {
                        workQueue.push(std::move(step));
                    }
                    if (actual > 0)
                    {
                        queueNotEmpty.notify_all();
                    }
                    if (toRequest > actual)
                    {
                        slotsAvailable.notify_one();
                    }
                }

                if (actual > 0)
                {
                    foundWork = true;
                }
            }

            if (!foundWork && !stopping)
            {
                std::unique_lock<std::mutex> lock(stateMutex);
                slotsAvailable.wait_for(
                    lock, options.pollInterval, [this] { return stopping.load(); }
                );
            }
        }
    }

    void runExecutor()
    {
        while (true)
        {
            WorkflowStepExecution step;
            {
                std::unique_lock<std::mutex> lock(stateMutex);
                queueNotEmpty.wait(lock, [this] { return !workQueue.empty() || stopping.load(); });
                if (workQueue.empty())
                {
                    return;
                }
                step = std::move(workQueue.front());
                workQueue.pop();
            }

            executeStep(step);

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                ++idleExecutors;
                slotsAvailable.notify_one();
            }
        }
    }

    void executeStep(const WorkflowStepExecution& step)
    {
        const auto it = handlers.find(step.stepName);

        if (it == handlers.end())
        {
            try
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                client.failWorkflowStep(
                    FailWorkflowStepRequest{
                        .workflowExecutionId = step.workflowExecutionId,
                        .stepName = step.stepName,
                        .workerId = workerId,
                        .reason = "no handler registered for step: " + step.stepName,
                    }
                );
            }
            catch (...)
            {
            }
            return;
        }

        json::Value output = json::Value::object();
        bool succeeded = false;
        std::string failureReason;

        {
            KeepAliveThread keepAlive(
                options.keepAliveInterval,
                [&]
                {
                    std::lock_guard<std::mutex> lock(clientMutex);
                    client.keepAliveWorkflowStep(
                        KeepAliveWorkflowStepRequest{
                            .workflowExecutionId = step.workflowExecutionId,
                            .stepName = step.stepName,
                            .workerId = workerId,
                        }
                    );
                }
            );

            try
            {
                output = it->second(step);
                succeeded = true;
            }
            catch (const std::exception& e)
            {
                failureReason = e.what();
            }
            catch (...)
            {
                failureReason = "unknown error in step handler";
            }
        }

        if (succeeded)
        {
            try
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                client.completeWorkflowStep(
                    CompleteWorkflowStepRequest{
                        .workflowExecutionId = step.workflowExecutionId,
                        .stepName = step.stepName,
                        .workerId = workerId,
                        .stepOutput = output,
                    }
                );
            }
            catch (...)
            {
            }
        }
        else
        {
            try
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                client.failWorkflowStep(
                    FailWorkflowStepRequest{
                        .workflowExecutionId = step.workflowExecutionId,
                        .stepName = step.stepName,
                        .workerId = workerId,
                        .reason = failureReason,
                    }
                );
            }
            catch (...)
            {
            }
        }
    }
};

WorkflowWorkerPool::WorkflowWorkerPool(
    WorkflowClient& client,
    std::vector<WorkflowDefinitionKey> workflowDefinitions,
    std::string workerId
)
    : WorkflowWorkerPool(
          client,
          std::move(workflowDefinitions),
          std::move(workerId),
          Options{}
      )
{
}

WorkflowWorkerPool::WorkflowWorkerPool(
    WorkflowClient& client,
    std::vector<WorkflowDefinitionKey> workflowDefinitions,
    std::string workerId,
    Options options
)
    : impl_(
          std::make_unique<Impl>(
              client,
              std::move(workflowDefinitions),
              std::move(workerId),
              std::move(options)
          )
      )
{
}

WorkflowWorkerPool::~WorkflowWorkerPool()
{
    impl_->stop();
}

void WorkflowWorkerPool::registerStep(
    const std::string& stepName,
    StepHandler handler
)
{
    impl_->handlers[stepName] = std::move(handler);
}

void WorkflowWorkerPool::start()
{
    impl_->start();
}

void WorkflowWorkerPool::stop()
{
    impl_->stop();
}

} // namespace workflow
