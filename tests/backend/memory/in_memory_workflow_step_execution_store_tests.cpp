#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

TEST_CASE("in-memory workflow step execution store saves and finds step executions")
{
    InMemoryWorkflowStepExecutionStore store;
    const auto stepExecution = makeStepExecution();

    store.save(stepExecution);

    const auto found = store.find("wfexec-001", "validateOrder", 0);

    REQUIRE(found.has_value());
    REQUIRE(found->workflowExecutionId == "wfexec-001");
    REQUIRE(found->workflowName == "orderProcessing");
    REQUIRE(found->workflowVersion == 1);
    REQUIRE(found->stepName == "validateOrder");
    REQUIRE(found->attempt == 0);
    REQUIRE(found->status == StepExecutionStatus::Pending);
    REQUIRE_FALSE(found->workerId.has_value());
    REQUIRE_FALSE(found->failureReason.has_value());
    REQUIRE(store.size() == 1);
}

TEST_CASE("in-memory workflow step execution store returns nullopt for missing step execution")
{
    InMemoryWorkflowStepExecutionStore store;

    const auto found = store.find("wfexec-001", "missingStep", 0);

    REQUIRE_FALSE(found.has_value());
}

TEST_CASE("in-memory workflow step execution store save replaces existing step execution")
{
    InMemoryWorkflowStepExecutionStore store;
    auto stepExecution = makeStepExecution();

    store.save(stepExecution);

    stepExecution.status = StepExecutionStatus::Failed;
    stepExecution.failureReason = "failed";
    store.save(stepExecution);

    const auto found = store.find("wfexec-001", "validateOrder", 0);

    REQUIRE(found.has_value());
    REQUIRE(found->status == StepExecutionStatus::Failed);
    REQUIRE(found->failureReason.has_value());
    REQUIRE(found->failureReason.value() == "failed");
    REQUIRE(store.size() == 1);
}

TEST_CASE("in-memory workflow step execution store stores attempts independently")
{
    InMemoryWorkflowStepExecutionStore store;

    store.save(makeStepExecution("wfexec-001", "validateOrder", 0));
    store.save(makeStepExecution("wfexec-001", "validateOrder", 1));

    const auto firstAttempt = store.find("wfexec-001", "validateOrder", 0);
    const auto secondAttempt = store.find("wfexec-001", "validateOrder", 1);

    REQUIRE(firstAttempt.has_value());
    REQUIRE(secondAttempt.has_value());
    REQUIRE(firstAttempt->attempt == 0);
    REQUIRE(secondAttempt->attempt == 1);
    REQUIRE(store.size() == 2);
}

TEST_CASE("in-memory workflow step execution store updates existing step execution")
{
    InMemoryWorkflowStepExecutionStore store;
    auto stepExecution = makeStepExecution();

    store.save(stepExecution);

    stepExecution.status = StepExecutionStatus::Completed;
    stepExecution.workerId = "worker-001";
    stepExecution.output["valid"] = true;
    store.update(stepExecution);

    const auto found = store.find("wfexec-001", "validateOrder", 0);

    REQUIRE(found.has_value());
    REQUIRE(found->status == StepExecutionStatus::Completed);
    REQUIRE(found->workerId.has_value());
    REQUIRE(found->workerId.value() == "worker-001");
    REQUIRE(found->output.contains("valid"));
    REQUIRE(found->output.at("valid").asBool());
}

TEST_CASE("in-memory workflow step execution store update throws for missing step execution")
{
    InMemoryWorkflowStepExecutionStore store;
    const auto stepExecution = makeStepExecution();

    REQUIRE_THROWS_AS(store.update(stepExecution), std::runtime_error);
}

TEST_CASE("in-memory workflow step execution store removes step executions")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution());

    store.remove("wfexec-001", "validateOrder", 0);

    REQUIRE_FALSE(store.find("wfexec-001", "validateOrder", 0).has_value());
    REQUIRE(store.size() == 0);
}

TEST_CASE("in-memory workflow step execution store remove is idempotent for missing step execution")
{
    InMemoryWorkflowStepExecutionStore store;

    store.remove("wfexec-001", "validateOrder", 0);

    REQUIRE(store.size() == 0);
}

TEST_CASE("in-memory workflow step execution store clears all step executions")
{
    InMemoryWorkflowStepExecutionStore store;

    store.save(makeStepExecution("wfexec-001", "validateOrder", 0));
    store.save(makeStepExecution("wfexec-002", "chargePayment", 0));

    REQUIRE(store.size() == 2);

    store.clear();

    REQUIRE(store.size() == 0);
    REQUIRE_FALSE(store.find("wfexec-001", "validateOrder", 0).has_value());
    REQUIRE_FALSE(store.find("wfexec-002", "chargePayment", 0).has_value());
}

TEST_CASE("pollAndClaim claims pending matching steps only")
{
    InMemoryWorkflowStepExecutionStore store;

    store.save(makeStepExecution("wfexec-001", "validateOrder", 0));
    store.save(makeStepExecution("wfexec-002", "validateOrder", 0));

    auto completed = makeStepExecution("wfexec-003", "validateOrder", 0);
    completed.status = StepExecutionStatus::Completed;
    store.save(completed);

    auto failed = makeStepExecution("wfexec-004", "validateOrder", 0);
    failed.status = StepExecutionStatus::Failed;
    store.save(failed);

    auto claimedAlready = makeStepExecution("wfexec-005", "validateOrder", 0);
    claimedAlready.status = StepExecutionStatus::Claimed;
    claimedAlready.workerId = "worker-other";
    claimedAlready.leaseExpiresAt = std::chrono::system_clock::now() + std::chrono::seconds{10};
    store.save(claimedAlready);

    auto otherWorkflow = makeStepExecution("wfexec-006", "validateOrder", 0);
    otherWorkflow.workflowName = "invoiceProcessing";
    store.save(otherWorkflow);

    auto otherVersion = makeStepExecution("wfexec-007", "validateOrder", 0);
    otherVersion.workflowVersion = 2;
    store.save(otherVersion);

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 10, leaseDurations());

    REQUIRE(claimed.size() == 2);

    for (const auto& step : claimed)
    {
        REQUIRE(step.workflowName == "orderProcessing");
        REQUIRE(step.workflowVersion == 1);
        REQUIRE(step.status == StepExecutionStatus::Claimed);
        REQUIRE(step.workerId.has_value());
        REQUIRE(step.workerId.value() == "worker-001");
        REQUIRE(step.leaseExpiresAt.has_value());
    }

    REQUIRE(store.find("wfexec-001", "validateOrder", 0)->status == StepExecutionStatus::Claimed);
    REQUIRE(store.find("wfexec-002", "validateOrder", 0)->status == StepExecutionStatus::Claimed);
    REQUIRE(store.find("wfexec-003", "validateOrder", 0)->status == StepExecutionStatus::Completed);
    REQUIRE(store.find("wfexec-004", "validateOrder", 0)->status == StepExecutionStatus::Failed);
    REQUIRE(store.find("wfexec-005", "validateOrder", 0)->status == StepExecutionStatus::Claimed);
    REQUIRE(store.find("wfexec-006", "validateOrder", 0)->status == StepExecutionStatus::Pending);
    REQUIRE(store.find("wfexec-007", "validateOrder", 0)->status == StepExecutionStatus::Pending);
}

TEST_CASE("pollAndClaim respects maxResults")
{
    InMemoryWorkflowStepExecutionStore store;

    store.save(makeStepExecution("wfexec-001", "validateOrder", 0));
    store.save(makeStepExecution("wfexec-002", "validateOrder", 0));
    store.save(makeStepExecution("wfexec-003", "validateOrder", 0));

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 2, leaseDurations());

    REQUIRE(claimed.size() == 2);

    std::size_t stillPending = 0;
    for (const auto& workflowExecutionId : {"wfexec-001", "wfexec-002", "wfexec-003"})
    {
        const auto found = store.find(workflowExecutionId, "validateOrder", 0);
        REQUIRE(found.has_value());

        if (found->status == StepExecutionStatus::Pending)
        {
            ++stillPending;
        }
    }

    REQUIRE(stillPending == 1);
}

TEST_CASE("pollAndClaim does not claim already claimed steps with active leases")
{
    InMemoryWorkflowStepExecutionStore store;
    store.save(makeStepExecution("wfexec-001", "validateOrder", 0));

    const auto firstClaim =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());
    const auto secondClaim =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, leaseDurations());

    REQUIRE(firstClaim.size() == 1);
    REQUIRE(secondClaim.empty());

    const auto found = store.find("wfexec-001", "validateOrder", 0);

    REQUIRE(found.has_value());
    REQUIRE(found->status == StepExecutionStatus::Claimed);
    REQUIRE(found->workerId.has_value());
    REQUIRE(found->workerId.value() == "worker-001");
    REQUIRE(found->leaseExpiresAt.has_value());
}

TEST_CASE("pollAndClaim returns an empty vector when no pending matching steps exist")
{
    InMemoryWorkflowStepExecutionStore store;

    auto completed = makeStepExecution("wfexec-001", "validateOrder", 0);
    completed.status = StepExecutionStatus::Completed;
    store.save(completed);

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 10, leaseDurations());

    REQUIRE(claimed.empty());
}

TEST_CASE("pollAndClaim reclaims expired claimed steps")
{
    InMemoryWorkflowStepExecutionStore store;

    auto expiredClaim = makeStepExecution("wfexec-001", "validateOrder", 0);
    expiredClaim.status = StepExecutionStatus::Claimed;
    expiredClaim.workerId = "worker-old";
    expiredClaim.leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.save(expiredClaim);

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-new", 1, leaseDurations());

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].workflowExecutionId == "wfexec-001");
    REQUIRE(claimed[0].status == StepExecutionStatus::Claimed);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-new");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
}

TEST_CASE("pollAndClaim is atomic across concurrent workers")
{
    InMemoryWorkflowStepExecutionStore store;

    for (int i = 0; i < 20; ++i)
    {
        store.save(makeStepExecution("wfexec-" + std::to_string(i), "validateOrder", 0));
    }

    std::vector<WorkflowStepExecution> workerOneSteps;
    std::vector<WorkflowStepExecution> workerTwoSteps;

    std::thread workerOne(
        [&store, &workerOneSteps]()
        {
            workerOneSteps =
                store.pollAndClaim("orderProcessing", 1, "worker-001", 20, leaseDurations());
        }
    );

    std::thread workerTwo(
        [&store, &workerTwoSteps]()
        {
            workerTwoSteps =
                store.pollAndClaim("orderProcessing", 1, "worker-002", 20, leaseDurations());
        }
    );

    workerOne.join();
    workerTwo.join();

    REQUIRE((workerOneSteps.size() + workerTwoSteps.size()) == 20);

    std::size_t workerOnePersisted = 0;
    std::size_t workerTwoPersisted = 0;

    for (int i = 0; i < 20; ++i)
    {
        const auto found = store.find("wfexec-" + std::to_string(i), "validateOrder", 0);

        REQUIRE(found.has_value());
        REQUIRE(found->status == StepExecutionStatus::Claimed);
        REQUIRE(found->workerId.has_value());
        REQUIRE(found->leaseExpiresAt.has_value());

        if (found->workerId.value() == "worker-001")
        {
            ++workerOnePersisted;
        }
        else if (found->workerId.value() == "worker-002")
        {
            ++workerTwoPersisted;
        }
        else
        {
            FAIL("unexpected worker id");
        }
    }

    REQUIRE((workerOnePersisted + workerTwoPersisted) == 20);
}

TEST_CASE("in-memory workflow step execution store rejects invalid step execution values")
{
    InMemoryWorkflowStepExecutionStore store;

    auto emptyExecutionId = makeStepExecution("", "validateOrder", 0);
    auto emptyStepName = makeStepExecution("wfexec-001", "", 0);
    auto negativeAttempt = makeStepExecution("wfexec-001", "validateOrder", -1);
    auto emptyWorkflowName = makeStepExecution("wfexec-001", "validateOrder", 0);
    emptyWorkflowName.workflowName = "";
    auto invalidWorkflowVersion = makeStepExecution("wfexec-001", "validateOrder", 0);
    invalidWorkflowVersion.workflowVersion = 0;

    REQUIRE_THROWS_AS(store.save(emptyExecutionId), std::invalid_argument);
    REQUIRE_THROWS_AS(store.save(emptyStepName), std::invalid_argument);
    REQUIRE_THROWS_AS(store.save(negativeAttempt), std::invalid_argument);
    REQUIRE_THROWS_AS(store.save(emptyWorkflowName), std::invalid_argument);
    REQUIRE_THROWS_AS(store.save(invalidWorkflowVersion), std::invalid_argument);

    REQUIRE_THROWS_AS(store.update(emptyExecutionId), std::invalid_argument);
    REQUIRE_THROWS_AS(store.update(emptyStepName), std::invalid_argument);
    REQUIRE_THROWS_AS(store.update(negativeAttempt), std::invalid_argument);
    REQUIRE_THROWS_AS(store.update(emptyWorkflowName), std::invalid_argument);
    REQUIRE_THROWS_AS(store.update(invalidWorkflowVersion), std::invalid_argument);
}

TEST_CASE("in-memory workflow step execution store rejects invalid identity values")
{
    InMemoryWorkflowStepExecutionStore store;

    REQUIRE_THROWS_AS(store.find("", "validateOrder", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("wfexec-001", "", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("wfexec-001", "validateOrder", -1), std::invalid_argument);

    REQUIRE_THROWS_AS(store.remove("", "validateOrder", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove("wfexec-001", "", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove("wfexec-001", "validateOrder", -1), std::invalid_argument);
}

TEST_CASE("pollAndClaim rejects invalid request values")
{
    InMemoryWorkflowStepExecutionStore store;

    REQUIRE_THROWS_AS(
        store.pollAndClaim("", 1, "worker-001", 1, leaseDurations()), std::invalid_argument
    );
    REQUIRE_THROWS_AS(
        store.pollAndClaim("orderProcessing", 0, "worker-001", 1, leaseDurations()),
        std::invalid_argument
    );
    REQUIRE_THROWS_AS(
        store.pollAndClaim("orderProcessing", 1, "", 1, leaseDurations()), std::invalid_argument
    );
    REQUIRE_THROWS_AS(
        store.pollAndClaim("orderProcessing", 1, "worker-001", 0, leaseDurations()),
        std::invalid_argument
    );

    REQUIRE_THROWS_AS(
        store.pollAndClaim(
            "orderProcessing", 1, "worker-001", 1,
            std::map<std::string, std::chrono::seconds>{{"validateOrder", std::chrono::seconds{0}}}
        ),
        std::invalid_argument
    );
}
