#include "catch2/catch_amalgamated.hpp"
#include "wf/json.hpp"
#include "wf/logic/step_output_routing_logic.hpp"
#include "wf/workflow_logic.hpp"

using workflow::NextStepDecision;
using workflow::StepCompletionContext;
using workflow::logic::StepOutputRoutingLogic;

namespace
{

StepCompletionContext makeContext(workflow::json::Value stepOutput)
{
    StepCompletionContext ctx;
    ctx.workflowName = "orderProcessing";
    ctx.workflowVersion = 1;
    ctx.workflowExecutionId = "exec-001";
    ctx.completedStepName = "validateOrder";
    ctx.input = workflow::json::Value::object();
    ctx.state = workflow::json::Value::object();
    ctx.stepOutput = std::move(stepOutput);
    return ctx;
}

} // namespace

TEST_CASE("StepOutputRoutingLogic routes to nextStep from output")
{
    StepOutputRoutingLogic logic;

    SECTION("nextStep present routes to named step")
    {
        workflow::json::Value::Object output;
        output["nextStep"] = std::string("chargePayment");

        const auto decision = logic.decideNextStep(makeContext(workflow::json::Value(output)));

        REQUIRE_FALSE(decision.workflowComplete);
        REQUIRE(decision.nextStepName == "chargePayment");
    }

    SECTION("nextStep absent marks workflow complete")
    {
        const auto decision = logic.decideNextStep(makeContext(workflow::json::Value::object()));

        REQUIRE(decision.workflowComplete);
        REQUIRE_FALSE(decision.nextStepName.has_value());
    }

    SECTION("nextStep empty string marks workflow complete")
    {
        workflow::json::Value::Object output;
        output["nextStep"] = std::string("");

        const auto decision = logic.decideNextStep(makeContext(workflow::json::Value(output)));

        REQUIRE(decision.workflowComplete);
    }

    SECTION("nextStep non-string marks workflow complete")
    {
        workflow::json::Value::Object output;
        output["nextStep"] = 42;

        const auto decision = logic.decideNextStep(makeContext(workflow::json::Value(output)));

        REQUIRE(decision.workflowComplete);
    }

    SECTION("step can loop back to itself")
    {
        workflow::json::Value::Object output;
        output["nextStep"] = std::string("validateOrder");

        const auto decision = logic.decideNextStep(makeContext(workflow::json::Value(output)));

        REQUIRE_FALSE(decision.workflowComplete);
        REQUIRE(decision.nextStepName == "validateOrder");
    }
}
