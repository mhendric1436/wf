#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"

#include <stdexcept>

using workflow::WorkflowExecution;
using workflow::WorkflowExecutionStatus;
using workflow::backend::memory::InMemoryWorkflowExecutionStore;

namespace
{

WorkflowExecution makeExecution(const std::string& workflowExecutionId = "wfexec-001")
{
    WorkflowExecution execution;
    execution.workflowExecutionId = workflowExecutionId;
    execution.workflowName = "orderProcessing";
    execution.workflowVersion = 1;
    execution.status = WorkflowExecutionStatus::Running;
    execution.currentStepName = "validateOrder";
    execution.input = workflow::json::Value::object();
    execution.state = workflow::json::Value::object();
    execution.currentStepAttempt = 0;
    return execution;
}

} // namespace

TEST_CASE("in-memory workflow execution store saves and finds executions")
{
    InMemoryWorkflowExecutionStore store;
    const auto execution = makeExecution();

    store.save(execution);

    const auto found = store.find("wfexec-001");

    REQUIRE(found.has_value());
    REQUIRE(found->workflowExecutionId == "wfexec-001");
    REQUIRE(found->workflowName == "orderProcessing");
    REQUIRE(found->workflowVersion == 1);
    REQUIRE(found->currentStepName == "validateOrder");
    REQUIRE(store.size() == 1);
}

TEST_CASE("in-memory workflow execution store returns nullopt for missing executions")
{
    InMemoryWorkflowExecutionStore store;

    const auto found = store.find("missing-execution");

    REQUIRE_FALSE(found.has_value());
}

TEST_CASE("in-memory workflow execution store save replaces existing execution")
{
    InMemoryWorkflowExecutionStore store;
    auto execution = makeExecution();

    store.save(execution);

    execution.currentStepName = "chargePayment";
    store.save(execution);

    const auto found = store.find("wfexec-001");

    REQUIRE(found.has_value());
    REQUIRE(found->currentStepName == "chargePayment");
    REQUIRE(store.size() == 1);
}

TEST_CASE("in-memory workflow execution store updates existing execution")
{
    InMemoryWorkflowExecutionStore store;
    auto execution = makeExecution();

    store.save(execution);

    execution.currentStepName = "chargePayment";
    execution.currentStepAttempt = 1;
    store.update(execution);

    const auto found = store.find("wfexec-001");

    REQUIRE(found.has_value());
    REQUIRE(found->currentStepName == "chargePayment");
    REQUIRE(found->currentStepAttempt == 1);
}

TEST_CASE("in-memory workflow execution store update throws for missing execution")
{
    InMemoryWorkflowExecutionStore store;
    const auto execution = makeExecution();

    REQUIRE_THROWS_AS(store.update(execution), std::runtime_error);
}

TEST_CASE("in-memory workflow execution store removes executions")
{
    InMemoryWorkflowExecutionStore store;
    store.save(makeExecution());

    store.remove("wfexec-001");

    REQUIRE_FALSE(store.find("wfexec-001").has_value());
    REQUIRE(store.size() == 0);
}

TEST_CASE("in-memory workflow execution store rejects empty execution ids")
{
    InMemoryWorkflowExecutionStore store;
    auto execution = makeExecution("");

    REQUIRE_THROWS_AS(store.save(execution), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find(""), std::invalid_argument);
    REQUIRE_THROWS_AS(store.update(execution), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove(""), std::invalid_argument);
}
