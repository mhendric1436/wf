#include "catch2/catch_amalgamated.hpp"
#include "mt/json.hpp"
#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/backend/sqlite/sqlite_workflow_execution_store.hpp"

#include <chrono>
#include <stdexcept>

using workflow::WorkflowExecution;
using workflow::WorkflowExecutionStatus;
using workflow::backend::sqlite::SQLiteDatabase;
using workflow::backend::sqlite::SQLiteWorkflowExecutionStore;

namespace
{

WorkflowExecution makeExecution(const std::string& id = "wfexec-001")
{
    WorkflowExecution exec;
    exec.workflowExecutionId = id;
    exec.workflowName = "orderProcessing";
    exec.workflowVersion = 1;
    exec.status = WorkflowExecutionStatus::Running;
    exec.currentStepName = "validateOrder";
    exec.currentStepAttempt = 0;
    exec.input = mt::Json(mt::Json::Object{});
    exec.state = mt::Json(mt::Json::Object{});
    return exec;
}

} // namespace

TEST_CASE("sqlite workflow execution store saves and finds executions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    store.save(makeExecution());

    const auto found = store.find("wfexec-001");

    REQUIRE(found.has_value());
    REQUIRE(found->workflowExecutionId == "wfexec-001");
    REQUIRE(found->workflowName == "orderProcessing");
    REQUIRE(found->workflowVersion == 1);
    REQUIRE(found->status == WorkflowExecutionStatus::Running);
    REQUIRE(found->currentStepName == "validateOrder");
    REQUIRE(found->currentStepAttempt == 0);
}

TEST_CASE("sqlite workflow execution store returns nullopt for missing executions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    REQUIRE_FALSE(store.find("wfexec-missing").has_value());
}

TEST_CASE("sqlite workflow execution store updates executions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    auto exec = makeExecution();
    store.save(exec);

    exec.status = WorkflowExecutionStatus::Completed;
    exec.completedAt = std::chrono::system_clock::now();
    store.update(exec);

    const auto found = store.find("wfexec-001");

    REQUIRE(found.has_value());
    REQUIRE(found->status == WorkflowExecutionStatus::Completed);
    REQUIRE(found->completedAt.has_value());
}

TEST_CASE("sqlite workflow execution store update throws for missing execution")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    REQUIRE_THROWS_AS(store.update(makeExecution("wfexec-missing")), std::runtime_error);
}

TEST_CASE("sqlite workflow execution store roundtrips optional fields")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    auto exec = makeExecution();
    exec.failureReason = "something failed";
    exec.startedAt = std::chrono::system_clock::now();
    exec.completedAt = std::chrono::system_clock::now();
    store.save(exec);

    const auto found = store.find("wfexec-001");

    REQUIRE(found.has_value());
    REQUIRE(found->failureReason.has_value());
    REQUIRE(found->failureReason.value() == "something failed");
    REQUIRE(found->startedAt.has_value());
    REQUIRE(found->completedAt.has_value());
}

TEST_CASE("sqlite workflow execution store roundtrips all statuses")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    const std::vector<WorkflowExecutionStatus> statuses = {
        WorkflowExecutionStatus::Running,
        WorkflowExecutionStatus::Completed,
        WorkflowExecutionStatus::Failed,
        WorkflowExecutionStatus::Canceled,
    };

    for (const auto status : statuses)
    {
        auto exec = makeExecution("wfexec-status");
        exec.status = status;
        store.save(exec);

        const auto found = store.find("wfexec-status");
        REQUIRE(found.has_value());
        REQUIRE(found->status == status);
    }
}

TEST_CASE("sqlite workflow execution store generateExecutionId produces unique ids")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    const auto id1 = store.generateExecutionId();
    const auto id2 = store.generateExecutionId();

    REQUIRE_FALSE(id1.empty());
    REQUIRE(id1 != id2);
}

TEST_CASE("sqlite workflow execution store rejects empty execution id")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowExecutionStore store(db);

    REQUIRE_THROWS_AS(store.find(""), std::invalid_argument);

    auto exec = makeExecution("");
    REQUIRE_THROWS_AS(store.save(exec), std::invalid_argument);
}
