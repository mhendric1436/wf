#include "catch2/catch_amalgamated.hpp"
#include "workflow/custom_json.hpp"
#include "workflow/workflow_parser.hpp"

#include <stdexcept>
#include <string>

using workflow::json::parse;
using workflow::parseWorkflowDefinition;
using workflow::parseWorkflowDefinitionText;
using workflow::validateWorkflowJson;

namespace {

const char* VALID_WORKFLOW_JSON = R"json(
{
  "workflowName": "orderProcessing",
  "workflowVersion": 1,
  "startWorkflowStepName": "validateOrder",
  "expectedExecutionTime": "PT10M",
  "steps": [
    {
      "name": "validateOrder",
      "expectedExecutionTime": "PT30S",
      "maxRetries": 2
    },
    {
      "name": "chargePayment",
      "expectedExecutionTime": "PT2M",
      "maxRetries": 3
    },
    {
      "name": "shipOrder",
      "expectedExecutionTime": "PT5M",
      "maxRetries": 1
    }
  ]
}
)json";

} // namespace

TEST_CASE("custom JSON parser parses object fields") {
    const auto value = parse(R"json({"name":"workflow","version":1,"enabled":true})json");

    REQUIRE(value.isObject());
    REQUIRE(value.at("name").asString() == "workflow");
    REQUIRE(value.at("version").asInt() == 1);
    REQUIRE(value.at("enabled").asBool());
}

TEST_CASE("custom JSON parser parses arrays") {
    const auto value = parse(R"json(["a","b","c"])json");

    REQUIRE(value.isArray());
    REQUIRE(value.asArray().size() == 3);
    REQUIRE(value[0].asString() == "a");
    REQUIRE(value[1].asString() == "b");
    REQUIRE(value[2].asString() == "c");
}

TEST_CASE("custom JSON parser rejects invalid JSON") {
    REQUIRE_THROWS_AS(parse(R"json({"name": )json"), workflow::json::JsonParseError);
}

TEST_CASE("valid workflow JSON passes validation") {
    const auto value = parse(VALID_WORKFLOW_JSON);

    const auto result = validateWorkflowJson(value);

    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

TEST_CASE("valid workflow JSON parses into workflow definition") {
    const auto workflow = parseWorkflowDefinitionText(VALID_WORKFLOW_JSON);

    REQUIRE(workflow.workflowName == "orderProcessing");
    REQUIRE(workflow.workflowVersion == 1);
    REQUIRE(workflow.startWorkflowStepName == "validateOrder");
    REQUIRE(workflow.expectedExecutionTime == "PT10M");

    REQUIRE(workflow.steps.size() == 3);

    REQUIRE(workflow.steps[0].name == "validateOrder");
    REQUIRE(workflow.steps[0].expectedExecutionTime.has_value());
    REQUIRE(workflow.steps[0].expectedExecutionTime.value() == "PT30S");
    REQUIRE(workflow.steps[0].maxRetries.has_value());
    REQUIRE(workflow.steps[0].maxRetries.value() == 2);

    REQUIRE(workflow.steps[1].name == "chargePayment");
    REQUIRE(workflow.steps[1].expectedExecutionTime.value() == "PT2M");
    REQUIRE(workflow.steps[1].maxRetries.value() == 3);

    REQUIRE(workflow.steps[2].name == "shipOrder");
    REQUIRE(workflow.steps[2].expectedExecutionTime.value() == "PT5M");
    REQUIRE(workflow.steps[2].maxRetries.value() == 1);
}

TEST_CASE("workflow name is required") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value.erase("workflowName");

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("workflow version must be greater than or equal to 1") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["workflowVersion"] = 0;

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("top-level expected execution time is required") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value.erase("expectedExecutionTime");

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("top-level expected execution time must be ISO-8601 duration") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["expectedExecutionTime"] = "10 minutes";

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("steps must contain at least one step") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["steps"] = workflow::json::Value::array();

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step name is required") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["steps"][0].erase("name");

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step names must be unique") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["steps"][1]["name"] = "validateOrder";

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("start workflow step name must exist in steps") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["startWorkflowStepName"] = "missingStep";

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step maxRetries must be non-negative") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["steps"][0]["maxRetries"] = -1;

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step expected execution time must be ISO-8601 duration") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["steps"][0]["expectedExecutionTime"] = "30 seconds";

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("additional step fields are preserved") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["steps"][0]["customField"] = "customValue";

    const auto workflow = parseWorkflowDefinition(value);

    REQUIRE(workflow.steps[0].additionalFields.contains("customField"));
    REQUIRE(workflow.steps[0].additionalFields.at("customField").asString() == "customValue");
}

TEST_CASE("additional top-level fields are rejected") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["customTopLevelField"] = "not allowed";

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("parser throws on invalid workflow") {
    auto value = parse(VALID_WORKFLOW_JSON);
    value["workflowVersion"] = 0;

    REQUIRE_THROWS_AS(parseWorkflowDefinition(value), std::invalid_argument);
}

TEST_CASE("parser throws on invalid JSON text") {
    const std::string invalidJson = R"json({ "workflowName": )json";

    REQUIRE_THROWS_AS(parseWorkflowDefinitionText(invalidJson), std::invalid_argument);
}
