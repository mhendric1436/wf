#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/backend/sqlite/sqlite_workflow_step_execution_store.hpp"

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>

using workflow::StepExecutionStatus;
using workflow::WorkflowStepExecution;
using workflow::backend::sqlite::SQLiteDatabase;
using workflow::backend::sqlite::SQLiteWorkflowStepExecutionStore;

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
    WorkflowStepExecution s;
    s.workflowExecutionId = workflowExecutionId;
    s.workflowName = "orderProcessing";
    s.workflowVersion = 1;
    s.stepName = stepName;
    s.attempt = attempt;
    s.status = StepExecutionStatus::Pending;
    s.input = workflow::json::Value::object();
    s.state = workflow::json::Value::object();
    s.output = workflow::json::Value::object();
    return s;
}

} // namespace

TEST_CASE("sqlite step store saves and finds step executions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());

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
}

TEST_CASE("sqlite step store returns nullopt for missing step execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    REQUIRE_FALSE(store.find("wfexec-001", "missingStep", 0).has_value());
}

TEST_CASE("sqlite step store save replaces existing step execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    auto step = makeStepExecution();
    store.save(step);

    step.status = StepExecutionStatus::Running;
    step.workerId = "worker-001";
    store.save(step);

    const auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());
    REQUIRE(found->status == StepExecutionStatus::Running);
    REQUIRE(found->workerId.has_value());
    REQUIRE(found->workerId.value() == "worker-001");
}

TEST_CASE("sqlite step store update modifies an existing step execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());

    auto step = store.find("wfexec-001", "validateOrder", 0).value();
    step.status = StepExecutionStatus::Completed;
    step.completedAt = std::chrono::system_clock::now();
    store.update(step);

    const auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());
    REQUIRE(found->status == StepExecutionStatus::Completed);
    REQUIRE(found->completedAt.has_value());
}

TEST_CASE("sqlite step store update throws for missing step execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    REQUIRE_THROWS_AS(store.update(makeStepExecution()), std::runtime_error);
}

TEST_CASE("sqlite step store remove deletes a step execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.remove("wfexec-001", "validateOrder", 0);

    REQUIRE_FALSE(store.find("wfexec-001", "validateOrder", 0).has_value());
}

TEST_CASE("sqlite step store remove is idempotent for missing step execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.remove("wfexec-001", "validateOrder", 0);
}

TEST_CASE("sqlite step store pollAndClaim claims a pending step")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].status == StepExecutionStatus::Running);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
}

TEST_CASE("sqlite step store pollAndClaim returns empty when no claimable steps")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE(claimed.empty());
}

TEST_CASE("sqlite step store cancelByExecution cancels all pending and running steps")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution("wfexec-001", "validateOrder", 0));
    store.save(makeStepExecution("wfexec-001", "chargePayment", 0));
    store.save(makeStepExecution("wfexec-002", "validateOrder", 0));

    store.cancelByExecution("wfexec-001");

    const auto step1 = store.find("wfexec-001", "validateOrder", 0);
    const auto step2 = store.find("wfexec-001", "chargePayment", 0);
    const auto step3 = store.find("wfexec-002", "validateOrder", 0);

    REQUIRE(step1.has_value());
    REQUIRE(step1->status == StepExecutionStatus::Canceled);
    REQUIRE(step2.has_value());
    REQUIRE(step2->status == StepExecutionStatus::Canceled);
    REQUIRE(step3.has_value());
    REQUIRE(step3->status == StepExecutionStatus::Pending);
}

TEST_CASE("sqlite step store cancelByExecution sets completedAt")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.cancelByExecution("wfexec-001");

    const auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());
    REQUIRE(found->completedAt.has_value());
}

TEST_CASE("sqlite step store findExpiredRunning returns steps with expired leases")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());

    found->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.update(*found);

    const auto expired = store.findExpiredRunning();

    REQUIRE(expired.size() == 1);
    REQUIRE(expired[0].workflowExecutionId == "wfexec-001");
}

TEST_CASE("sqlite step store findExpiredRunning excludes steps with valid leases")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    const auto expired = store.findExpiredRunning();

    REQUIRE(expired.empty());
}

TEST_CASE("sqlite step store roundtrips all step statuses")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    const std::vector<StepExecutionStatus> statuses = {
        StepExecutionStatus::Pending, StepExecutionStatus::Running,  StepExecutionStatus::Completed,
        StepExecutionStatus::Failed,  StepExecutionStatus::Canceled,
    };

    auto step = makeStepExecution();
    store.save(step);

    for (const auto status : statuses)
    {
        step.status = status;
        store.save(step);

        const auto found = store.find("wfexec-001", "validateOrder", 0);
        REQUIRE(found.has_value());
        REQUIRE(found->status == status);
    }
}

TEST_CASE("sqlite step store rejects invalid identities")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    REQUIRE_THROWS_AS(store.find("", "validateOrder", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("wfexec-001", "", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("wfexec-001", "validateOrder", -1), std::invalid_argument);
}
