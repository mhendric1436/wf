#include "catch2/catch_amalgamated.hpp"
#include "mt/json.hpp"
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
    s.input = mt::Json(mt::Json::Object{});
    s.state = mt::Json(mt::Json::Object{});
    s.output = mt::Json(mt::Json::Object{});
    return s;
}

} // namespace

TEST_CASE("sqlite pollAndClaim assigns a lease expiration")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());

    const auto beforeClaim = std::chrono::system_clock::now();
    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());
    const auto afterClaim = std::chrono::system_clock::now();

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].status == StepExecutionStatus::Running);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
    REQUIRE(claimed[0].leaseExpiresAt.value() >= beforeClaim + std::chrono::seconds{10});
    REQUIRE(claimed[0].leaseExpiresAt.value() <= afterClaim + std::chrono::seconds{10});
}

TEST_CASE("sqlite keepAlive extends an active lease for the owning worker")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE(claimed.size() == 1);
    const auto originalLease = claimed[0].leaseExpiresAt.value();

    const auto kept =
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-001", std::chrono::seconds{60});

    REQUIRE(kept.status == StepExecutionStatus::Running);
    REQUIRE(kept.leaseExpiresAt.has_value());
    REQUIRE(kept.leaseExpiresAt.value() > originalLease);
}

TEST_CASE("sqlite keepAlive rejects a different worker")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE_THROWS_AS(
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-002", std::chrono::seconds{60}),
        std::runtime_error
    );
}

TEST_CASE("sqlite keepAlive rejects an expired lease")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

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

TEST_CASE("sqlite pollAndClaim reclaims an expired running step")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    auto found = store.find("wfexec-001", "validateOrder", 0);
    REQUIRE(found.has_value());
    found->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.update(*found);

    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, leaseDurations());

    REQUIRE(reclaimed.size() == 1);
    REQUIRE(reclaimed[0].status == StepExecutionStatus::Running);
    REQUIRE(reclaimed[0].workerId.value() == "worker-002");
    REQUIRE(reclaimed[0].leaseExpiresAt.value() > std::chrono::system_clock::now());
}

TEST_CASE("sqlite pollAndClaim does not reclaim an unexpired running step")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, leaseDurations());

    REQUIRE(reclaimed.empty());
}

TEST_CASE("sqlite pollAndClaim sets startedAt within the claim window")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());

    const auto before = std::chrono::system_clock::now();
    const auto claimed =
        store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());
    const auto after = std::chrono::system_clock::now();

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].startedAt.has_value());
    REQUIRE(claimed[0].startedAt.value() >= before);
    REQUIRE(claimed[0].startedAt.value() <= after);
}

TEST_CASE("sqlite pollAndClaim resets startedAt on reclaim")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    auto found = store.find("wfexec-001", "validateOrder", 0);
    found->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    store.update(*found);

    const auto before = std::chrono::system_clock::now();
    const auto reclaimed =
        store.pollAndClaim("orderProcessing", 1, "worker-002", 1, leaseDurations());
    const auto after = std::chrono::system_clock::now();

    REQUIRE(reclaimed.size() == 1);
    REQUIRE(reclaimed[0].startedAt.has_value());
    REQUIRE(reclaimed[0].startedAt.value() >= before);
    REQUIRE(reclaimed[0].startedAt.value() <= after);
}

TEST_CASE("sqlite pollAndClaim rejects invalid lease duration maps")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

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

TEST_CASE("sqlite keepAlive rejects invalid lease duration")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowStepExecutionStore store(db);

    store.save(makeStepExecution());
    store.pollAndClaim("orderProcessing", 1, "worker-001", 1, leaseDurations());

    REQUIRE_THROWS_AS(
        store.keepAlive("wfexec-001", "validateOrder", 0, "worker-001", std::chrono::seconds{0}),
        std::invalid_argument
    );
}
