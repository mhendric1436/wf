#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"

#include <stdexcept>

using workflow::WorkflowDefinition;
using workflow::WorkflowStep;
using workflow::backend::memory::InMemoryWorkflowDefinitionStore;

namespace
{

WorkflowDefinition makeDefinition(
    const std::string& workflowName = "orderProcessing",
    int workflowVersion = 1
)
{
    WorkflowDefinition definition;
    definition.workflowName = workflowName;
    definition.workflowVersion = workflowVersion;
    definition.startWorkflowStepName = "validateOrder";
    definition.expectedExecutionTime = "PT10M";
    definition.steps = {
        WorkflowStep{
            .name = "validateOrder",
            .expectedExecutionTime = "PT30S",
            .maxRetries = 2,
        },
        WorkflowStep{
            .name = "chargePayment",
            .expectedExecutionTime = "PT2M",
            .maxRetries = 3,
        },
    };
    return definition;
}

} // namespace

TEST_CASE("in-memory workflow definition store saves and finds definitions")
{
    InMemoryWorkflowDefinitionStore store;
    const auto definition = makeDefinition();

    store.save(definition);

    const auto found = store.find("orderProcessing", 1);

    REQUIRE(found.has_value());
    REQUIRE(found->workflowName == "orderProcessing");
    REQUIRE(found->workflowVersion == 1);
    REQUIRE(found->startWorkflowStepName == "validateOrder");
    REQUIRE(store.size() == 1);
}

TEST_CASE("in-memory workflow definition store returns nullopt for missing definitions")
{
    InMemoryWorkflowDefinitionStore store;

    const auto found = store.find("missingWorkflow", 1);

    REQUIRE_FALSE(found.has_value());
}

TEST_CASE("in-memory workflow definition store lists saved definitions in key order")
{
    InMemoryWorkflowDefinitionStore store;

    store.save(makeDefinition("orderProcessing", 2));
    store.save(makeDefinition("orderProcessing", 1));
    store.save(makeDefinition("invoiceProcessing", 1));

    const auto keys = store.list();

    REQUIRE(keys.size() == 3);
    REQUIRE(keys[0].workflowName == "invoiceProcessing");
    REQUIRE(keys[0].workflowVersion == 1);
    REQUIRE(keys[1].workflowName == "orderProcessing");
    REQUIRE(keys[1].workflowVersion == 1);
    REQUIRE(keys[2].workflowName == "orderProcessing");
    REQUIRE(keys[2].workflowVersion == 2);
}

TEST_CASE("in-memory workflow definition store removes definitions")
{
    InMemoryWorkflowDefinitionStore store;
    store.save(makeDefinition());

    store.remove("orderProcessing", 1);

    REQUIRE_FALSE(store.find("orderProcessing", 1).has_value());
    REQUIRE(store.size() == 0);
}

TEST_CASE("in-memory workflow definition store remove is idempotent for missing definitions")
{
    InMemoryWorkflowDefinitionStore store;

    store.remove("orderProcessing", 1);

    REQUIRE(store.size() == 0);
}

TEST_CASE("in-memory workflow definition store replaces existing definition")
{
    InMemoryWorkflowDefinitionStore store;
    auto definition = makeDefinition();

    store.save(definition);

    definition.expectedExecutionTime = "PT20M";
    store.save(definition);

    const auto found = store.find("orderProcessing", 1);

    REQUIRE(found.has_value());
    REQUIRE(found->expectedExecutionTime == "PT20M");
    REQUIRE(store.size() == 1);
}

TEST_CASE("in-memory workflow definition store rejects invalid keys")
{
    InMemoryWorkflowDefinitionStore store;

    auto emptyName = makeDefinition("", 1);
    auto invalidVersion = makeDefinition("orderProcessing", 0);

    REQUIRE_THROWS_AS(store.save(emptyName), std::invalid_argument);
    REQUIRE_THROWS_AS(store.save(invalidVersion), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("", 1), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("orderProcessing", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove("", 1), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove("orderProcessing", 0), std::invalid_argument);
}
