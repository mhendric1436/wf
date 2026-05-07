#include "catch2/catch_amalgamated.hpp"
#include "mt/errors.hpp"
#include "mt/json.hpp"
#include "mt/json_parser.hpp"
#include "wf/workflow_json.hpp"

#include <stdexcept>
#include <string>

using workflow::parseWorkflowDefinition;
using workflow::parseWorkflowDefinitionText;
using workflow::validateWorkflowJson;

namespace
{

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

TEST_CASE("custom JSON parser parses object fields")
{
    const auto value =
        mt::JsonParser(R"json({"name":"workflow","version":1,"enabled":true})json").parse();

    REQUIRE(value.is_object());
    REQUIRE(value.at("name").as_string() == "workflow");
    REQUIRE(static_cast<int>(value.at("version").as_int64()) == 1);
    REQUIRE(value.at("enabled").as_bool());
}

TEST_CASE("custom JSON parser parses arrays")
{
    const auto value = mt::JsonParser(R"json(["a","b","c"])json").parse();

    REQUIRE(value.is_array());
    REQUIRE(value.as_array().size() == 3);
    REQUIRE(value.as_array().at(0).as_string() == "a");
    REQUIRE(value.as_array().at(1).as_string() == "b");
    REQUIRE(value.as_array().at(2).as_string() == "c");
}

TEST_CASE("custom JSON parser rejects invalid JSON")
{
    REQUIRE_THROWS_AS(mt::JsonParser(R"json({"name": )json").parse(), mt::BackendError);
}

TEST_CASE("valid workflow JSON passes validation")
{
    const auto value = mt::JsonParser(VALID_WORKFLOW_JSON).parse();

    const auto result = validateWorkflowJson(value);

    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

TEST_CASE("valid workflow JSON parses into workflow definition")
{
    const auto workflow = parseWorkflowDefinitionText(VALID_WORKFLOW_JSON);

    REQUIRE(workflow.workflowName == "orderProcessing");
    REQUIRE(workflow.workflowVersion == 1);
    REQUIRE(workflow.startWorkflowStepName == "validateOrder");
    REQUIRE(workflow.expectedExecutionTime == "PT10M");
    REQUIRE_FALSE(workflow.singleton);

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

TEST_CASE("workflow singleton flag is optional and parses when present")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["singleton"] = true;
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);
    const auto workflow = parseWorkflowDefinition(value);
    const auto json = workflow::toJson(workflow);

    REQUIRE(result.valid);
    REQUIRE(workflow.singleton);
    REQUIRE(json.at("singleton").as_bool());
}

TEST_CASE("workflow singleton flag must be a boolean")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["singleton"] = "true";
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("workflow name is required")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj.erase("workflowName");
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("workflow version must be greater than or equal to 1")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["workflowVersion"] = 0;
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("top-level expected execution time is required")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj.erase("expectedExecutionTime");
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("top-level expected execution time must be ISO-8601 duration")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["expectedExecutionTime"] = "10 minutes";
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("steps must contain at least one step")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["steps"] = mt::Json(mt::Json::Array{});
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step name is required")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    mt::Json::Array steps = obj.at("steps").as_array();
    mt::Json::Object step0 = steps.at(0).as_object();
    step0.erase("name");
    steps[0] = mt::Json(std::move(step0));
    obj["steps"] = mt::Json(std::move(steps));
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step names must be unique")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    mt::Json::Array steps = obj.at("steps").as_array();
    mt::Json::Object step1 = steps.at(1).as_object();
    step1["name"] = "validateOrder";
    steps[1] = mt::Json(std::move(step1));
    obj["steps"] = mt::Json(std::move(steps));
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("start workflow step name must exist in steps")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["startWorkflowStepName"] = "missingStep";
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step maxRetries must be non-negative")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    mt::Json::Array steps = obj.at("steps").as_array();
    mt::Json::Object step0 = steps.at(0).as_object();
    step0["maxRetries"] = -1;
    steps[0] = mt::Json(std::move(step0));
    obj["steps"] = mt::Json(std::move(steps));
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("step expected execution time must be ISO-8601 duration")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    mt::Json::Array steps = obj.at("steps").as_array();
    mt::Json::Object step0 = steps.at(0).as_object();
    step0["expectedExecutionTime"] = "30 seconds";
    steps[0] = mt::Json(std::move(step0));
    obj["steps"] = mt::Json(std::move(steps));
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("additional step fields are preserved")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    mt::Json::Array steps = obj.at("steps").as_array();
    mt::Json::Object step0 = steps.at(0).as_object();
    step0["customField"] = "customValue";
    steps[0] = mt::Json(std::move(step0));
    obj["steps"] = mt::Json(std::move(steps));
    auto value = mt::Json(std::move(obj));

    const auto workflow = parseWorkflowDefinition(value);

    REQUIRE(workflow.steps[0].additionalFields.contains("customField"));
    REQUIRE(workflow.steps[0].additionalFields.at("customField").as_string() == "customValue");
}

TEST_CASE("additional top-level fields are rejected")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["customTopLevelField"] = "not allowed";
    auto value = mt::Json(std::move(obj));

    const auto result = validateWorkflowJson(value);

    REQUIRE_FALSE(result.valid);
}

TEST_CASE("parser throws on invalid workflow")
{
    auto parsed = mt::JsonParser(VALID_WORKFLOW_JSON).parse();
    mt::Json::Object obj = parsed.as_object();
    obj["workflowVersion"] = 0;
    auto value = mt::Json(std::move(obj));

    REQUIRE_THROWS_AS(parseWorkflowDefinition(value), std::invalid_argument);
}

TEST_CASE("parser throws on invalid JSON text")
{
    const std::string invalidJson = R"json({ "workflowName": )json";

    REQUIRE_THROWS_AS(parseWorkflowDefinitionText(invalidJson), std::invalid_argument);
}
