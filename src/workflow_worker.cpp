#include "wf/workflow_worker.hpp"

#include "keep_alive_thread.hpp"
#include "wf/workflow_service.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace workflow
{

struct WorkflowWorker::Impl
{
    WorkflowClient& client;
    std::string workflowName;
    int workflowVersion;
    std::string workerId;
    Options options;

    std::map<std::string, StepHandler> handlers;

    std::atomic<bool> stopping{false};
    std::mutex waitMutex;
    std::condition_variable waitCv;
    std::thread pollThread;

    Impl(
        WorkflowClient& client_,
        std::string workflowName_,
        int workflowVersion_,
        std::string workerId_,
        Options options_
    )
        : client(client_),
          workflowName(std::move(workflowName_)),
          workflowVersion(workflowVersion_),
          workerId(std::move(workerId_)),
          options(options_)
    {
    }

    void run()
    {
        while (!stopping)
        {
            bool polled = false;

            try
            {
                const auto response = client.pollAndClaimWorkflowSteps(
                    PollAndClaimWorkflowStepsRequest{
                        .workflowName = workflowName,
                        .workflowVersion = workflowVersion,
                        .workerId = workerId,
                        .maxResults = 1,
                    }
                );

                if (!response.steps.empty())
                {
                    polled = true;
                    executeStep(response.steps[0]);
                }
            }
            catch (...)
            {
            }

            if (!polled)
            {
                std::unique_lock<std::mutex> lock(waitMutex);
                waitCv.wait_for(lock, options.pollInterval, [this] { return stopping.load(); });
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

        mt::Json output = mt::Json(mt::Json::Object{});
        bool succeeded = false;
        std::string failureReason;

        {
            KeepAliveThread keepAlive(
                options.keepAliveInterval,
                [&]
                {
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

            // KeepAliveThread destructor fires here, stopping pings before
            // the complete/fail call below.
        }

        if (succeeded)
        {
            try
            {
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

WorkflowWorker::WorkflowWorker(
    WorkflowClient& client,
    std::string workflowName,
    int workflowVersion,
    std::string workerId
)
    : WorkflowWorker(
          client,
          std::move(workflowName),
          workflowVersion,
          std::move(workerId),
          Options{}
      )
{
}

WorkflowWorker::WorkflowWorker(
    WorkflowClient& client,
    std::string workflowName,
    int workflowVersion,
    std::string workerId,
    Options options
)
    : impl_(
          std::make_unique<Impl>(
              client,
              std::move(workflowName),
              workflowVersion,
              std::move(workerId),
              options
          )
      )
{
}

WorkflowWorker::~WorkflowWorker()
{
    stop();
}

void WorkflowWorker::registerStep(
    const std::string& stepName,
    StepHandler handler
)
{
    impl_->handlers[stepName] = std::move(handler);
}

void WorkflowWorker::start()
{
    impl_->stopping = false;
    impl_->pollThread = std::thread([this] { impl_->run(); });
}

void WorkflowWorker::stop()
{
    if (!impl_->pollThread.joinable())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->waitMutex);
        impl_->stopping = true;
    }
    impl_->waitCv.notify_all();
    impl_->pollThread.join();
}

} // namespace workflow
