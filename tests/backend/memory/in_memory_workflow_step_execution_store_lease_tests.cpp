#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

using workflow::StepExecutionStatus;
using workflow::WorkflowStepExecution;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;

namespace
{

WorkflowStepExecution makeStepExecution(
    const std::string& workflowExecutionId = "wfexec-001",
    const std::string& stepName = "validateOrder",
    int attempt = 0
)
{
    WorkflowStepExecution stepExecution;
    stepExecution.workflowExecutionId = workflowExecutionId;
    stepExecution.workflowName = "orderProcessing";
    stepExecution.workflowVersion = 1;
    stepExecution.stepName = stepName;
    stepExecution.attempt = attempt;
    stepExecution.status = StepExecutionStatus::Pending;
    stepExecution.input = workflow::json::Value::object();
    stepExecution.state = workflow::json::Value::object();
    stepExecution.output = workflow::json::Value::object();
    return stepExecution;
}

} // namespace

TEST_CASE("pollAndClaim assigns a lease expiration")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, std::chrono::seconds{60});

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].status == StepExecutionStatus::Claimed);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
    REQUIRE(claimed[0].leaseExpiresAt.value() > std::chrono::system_clock::now());
}

TEST_CASE("keepAlive extends an active lease for the owning worker")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, std::chrono::seconds{60});

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].leaseExpiresAt.has_value());

    const auto originalLeaseExpiresAt = claimed[0].leaseExpiresAt.value();

    std::this_thread::sleep_for(std::chrono::milliseconds{2});

    const auto keptAlive =
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-001", std::chrono::seconds{120});

    REQUIRE(keptAlive.status == StepExecutionStatus::Claimed);
    REQUIRE(keptAlive.workerId.has_value());
    REQUIRE(keptAlive.workerId.value() == "worker-001");
    REQUIRE(keptAlive.leaseExpiresAt.has_value());
    REQUIRE(keptAlive.leaseExpiresAt.value() > originalLeaseExpiresAt);
}

TEST_CASE("keepAlive rejects a different worker")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, std::chrono::seconds{60});

    REQUIRE_THROWS_AS(
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-002", std::chrono::seconds{60}),
        std::runtime_error
    );
}

TEST_CASE("keepAlive rejects an expired lease")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, std::chrono::seconds{60});

    auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());

    found->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.update(*found);

    REQUIRE_THROWS_AS(
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-001", std::chrono::seconds{60}),
        std::runtime_error
    );
}

TEST_CASE("pollAndClaim reclaims an expired claimed step")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, std::chrono::seconds{60});

    auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());

    found->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.update(*found);

    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, std::chrono::seconds{60});

    REQUIRE(reclaimed.size() == 1);
    REQUIRE(reclaimed[0].status == StepExecutionStatus::Claimed);
    REQUIRE(reclaimed[0].workerId.has_value());
    REQUIRE(reclaimed[0].workerId.value() == "worker-002");
    REQUIRE(reclaimed[0].leaseExpiresAt.has_value());
    REQUIRE(reclaimed[0].leaseExpiresAt.value() > std::chrono::system_clock::now());
}

TEST_CASE("pollAndClaim does not reclaim an unexpired claimed step")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, std::chrono::seconds{60});

    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, std::chrono::seconds{60});

    REQUIRE(reclaimed.empty());
}
