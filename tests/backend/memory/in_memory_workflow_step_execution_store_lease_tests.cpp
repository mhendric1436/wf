#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>

using workflow::StepExecutionStatus;
using workflow::WorkflowStepExecution;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;

namespace
{

std::map<
    std::string,
    std::chrono::seconds>
leaseDurations()
{
    return {
        {"validateOrder", std::chrono::seconds{10}},
        {"chargePayment", std::chrono::seconds{40}},
    };
}

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

    const auto beforeClaim = std::chrono::system_clock::now();

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    const auto afterClaim = std::chrono::system_clock::now();

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].status == StepExecutionStatus::Claimed);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());

    REQUIRE(claimed[0].leaseExpiresAt.value() >= beforeClaim + std::chrono::seconds{10});
    REQUIRE(claimed[0].leaseExpiresAt.value() <= afterClaim + std::chrono::seconds{10});
}

TEST_CASE("keepAlive extends an active lease for the owning worker")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].leaseExpiresAt.has_value());

    const auto originalLeaseExpiresAt = claimed[0].leaseExpiresAt.value();

    const auto keptAlive =
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-001", std::chrono::seconds{60});

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

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE_THROWS_AS(
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-002", std::chrono::seconds{60}),
        std::runtime_error
    );
}

TEST_CASE("keepAlive rejects an expired lease")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

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

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());

    found->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.update(*found);

    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, leaseDurations());

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

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, leaseDurations());

    REQUIRE(reclaimed.empty());
}

TEST_CASE("pollAndClaim rejects invalid lease duration maps")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    REQUIRE_THROWS_AS(
        store.pollAndClaim(
            "orderProcessing", 1, "worker-001", 1,
            std::map<std::string, std::chrono::seconds>{{"validateOrder", std::chrono::seconds{0}}}
        ),
        std::invalid_argument
    );

    REQUIRE_THROWS_AS(
        store.pollAndClaim(
            "orderProcessing", 1, "worker-001", 1,
            std::map<std::string, std::chrono::seconds>{{"", std::chrono::seconds{10}}}
        ),
        std::invalid_argument
    );
}

TEST_CASE("keepAlive rejects invalid lease duration")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE_THROWS_AS(
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-001", std::chrono::seconds{0}),
        std::invalid_argument
    );
}
