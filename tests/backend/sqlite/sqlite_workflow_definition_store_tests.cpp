#include "catch2/catch_amalgamated.hpp"
#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/backend/sqlite/sqlite_workflow_definition_store.hpp"

#include <stdexcept>

using workflow::WorkflowDefinition;
using workflow::WorkflowStep;
using workflow::backend::sqlite::SQLiteDatabase;
using workflow::backend::sqlite::SQLiteWorkflowDefinitionStore;

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

TEST_CASE("sqlite workflow definition store saves and finds definitions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    store.save(makeDefinition());

    const auto found = store.find("orderProcessing", 1);

    REQUIRE(found.has_value());
    REQUIRE(found->workflowName == "orderProcessing");
    REQUIRE(found->workflowVersion == 1);
    REQUIRE(found->startWorkflowStepName == "validateOrder");
    REQUIRE(found->expectedExecutionTime == "PT10M");
    REQUIRE(found->steps.size() == 2);
    REQUIRE(found->steps[0].name == "validateOrder");
    REQUIRE(found->steps[0].expectedExecutionTime.has_value());
    REQUIRE(found->steps[0].expectedExecutionTime.value() == "PT30S");
    REQUIRE(found->steps[0].maxRetries.has_value());
    REQUIRE(found->steps[0].maxRetries.value() == 2);
}

TEST_CASE("sqlite workflow definition store returns nullopt for missing definitions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    REQUIRE_FALSE(store.find("missing", 1).has_value());
}

TEST_CASE("sqlite workflow definition store lists saved definitions in key order")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

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

TEST_CASE("sqlite workflow definition store removes definitions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    store.save(makeDefinition());
    store.remove("orderProcessing", 1);

    REQUIRE_FALSE(store.find("orderProcessing", 1).has_value());
}

TEST_CASE("sqlite workflow definition store remove is idempotent for missing definitions")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    store.remove("orderProcessing", 1);

    REQUIRE(store.list().empty());
}

TEST_CASE("sqlite workflow definition store replaces existing definition")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    auto definition = makeDefinition();
    store.save(definition);

    definition.expectedExecutionTime = "PT20M";
    store.save(definition);

    const auto found = store.find("orderProcessing", 1);

    REQUIRE(found.has_value());
    REQUIRE(found->expectedExecutionTime == "PT20M");
    REQUIRE(store.list().size() == 1);
}

TEST_CASE("sqlite workflow definition store rejects invalid keys")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    auto emptyName = makeDefinition("", 1);
    auto invalidVersion = makeDefinition("orderProcessing", 0);

    REQUIRE_THROWS_AS(store.save(emptyName), std::invalid_argument);
    REQUIRE_THROWS_AS(store.save(invalidVersion), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("", 1), std::invalid_argument);
    REQUIRE_THROWS_AS(store.find("orderProcessing", 0), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove("", 1), std::invalid_argument);
    REQUIRE_THROWS_AS(store.remove("orderProcessing", 0), std::invalid_argument);
}

TEST_CASE("sqlite workflow definition store roundtrips step optional fields")
{
    SQLiteDatabase db(":memory:");
    SQLiteWorkflowDefinitionStore store(db);

    WorkflowDefinition def;
    def.workflowName = "minimal";
    def.workflowVersion = 1;
    def.startWorkflowStepName = "only";
    def.expectedExecutionTime = "PT1M";
    def.steps = {WorkflowStep{.name = "only"}};

    store.save(def);

    const auto found = store.find("minimal", 1);

    REQUIRE(found.has_value());
    REQUIRE(found->steps.size() == 1);
    REQUIRE(found->steps[0].name == "only");
    REQUIRE_FALSE(found->steps[0].expectedExecutionTime.has_value());
    REQUIRE_FALSE(found->steps[0].maxRetries.has_value());
}
