#include "catch2/catch_amalgamated.hpp"
#include "httplib/httplib.h"
#include "wf/backend/memory/in_memory_workflow_definition_store.hpp"
#include "wf/backend/memory/in_memory_workflow_execution_store.hpp"
#include "wf/backend/memory/in_memory_workflow_step_execution_store.hpp"
#include "wf/http/workflow_http_server.hpp"
#include "wf/json.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"

#include <string>
#include <thread>
#include <vector>

using workflow::NextStepDecision;
using workflow::StepCompletionContext;
using workflow::StepExecutionStatus;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowLogic;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowService;
using workflow::backend::memory::InMemoryWorkflowDefinitionStore;
using workflow::backend::memory::InMemoryWorkflowExecutionStore;
using workflow::backend::memory::InMemoryWorkflowStepExecutionStore;
using workflow::http::WorkflowHttpServer;

namespace
{

const char* VALID_WORKFLOW_JSON = R"json({
  "workflowName": "orderProcessing",
  "workflowVersion": 1,
  "startWorkflowStepName": "validateOrder",
  "expectedExecutionTime": "PT10M",
  "steps": [
    {"name": "validateOrder", "expectedExecutionTime": "PT30S", "maxRetries": 2},
    {"name": "chargePayment", "expectedExecutionTime": "PT2M", "maxRetries": 1}
  ]
})json";

class ScriptedWorkflowLogic final : public WorkflowLogic
{
  public:
    explicit ScriptedWorkflowLogic(std::vector<NextStepDecision> decisions = {})
        : decisions_(std::move(decisions))
    {
    }

    NextStepDecision decideNextStep(const StepCompletionContext&) override
    {
        if (nextIndex_ >= decisions_.size())
        {
            NextStepDecision d;
            d.workflowComplete = true;
            d.updatedState = workflow::json::Value::object();
            return d;
        }
        return decisions_.at(nextIndex_++);
    }

  private:
    std::vector<NextStepDecision> decisions_;
    std::size_t nextIndex_ = 0;
};

struct HttpTestContext
{
    InMemoryWorkflowDefinitionStore definitionStore;
    InMemoryWorkflowExecutionStore executionStore;
    InMemoryWorkflowStepExecutionStore stepExecutionStore;
    ScriptedWorkflowLogic logic;
    WorkflowOrchestrator orchestrator;
    WorkflowService service;
    WorkflowHttpServer server;
    int port;
    std::thread serverThread;
    httplib::Client client;

    explicit HttpTestContext(std::vector<NextStepDecision> decisions = {})
        : logic(std::move(decisions)),
          orchestrator(
              definitionStore,
              executionStore,
              stepExecutionStore,
              logic
          ),
          service(orchestrator),
          server(
              service,
              0
          ),
          port(server.bind()),
          serverThread([this] { server.start(); }),
          client(
              "localhost",
              port
          )
    {
    }

    ~HttpTestContext()
    {
        server.stop();
        serverThread.join();
    }

    void registerDefinition()
    {
        client.Post("/v1/workflow-definitions", VALID_WORKFLOW_JSON, "application/json");
    }

    std::string startExecution()
    {
        const auto res = client.Post(
            "/v1/workflow-executions",
            R"json({"workflowName":"orderProcessing","workflowVersion":1})json", "application/json"
        );
        return workflow::json::parse(res->body)["execution"]["workflowExecutionId"].asString();
    }

    std::string claimStep(const std::string& workerId = "worker-001")
    {
        const auto res = client.Post(
            "/v1/workflow-step-executions/poll-and-claim",
            R"json({"workflowName":"orderProcessing","workflowVersion":1,"workerId":"worker-001","maxResults":1})json",
            "application/json"
        );
        const auto body = workflow::json::parse(res->body);
        return body["steps"][0]["workflowExecutionId"].asString();
        (void)workerId;
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Validate
// -----------------------------------------------------------------------------

TEST_CASE("http server validate returns valid result for a valid definition")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-definitions/validate", VALID_WORKFLOW_JSON, "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["validation"]["valid"].asBool() == true);
    REQUIRE(body["validation"]["errors"].asArray().empty());
}

TEST_CASE("http server validate returns errors for an invalid definition")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-definitions/validate", R"json({"workflowName":""})json", "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["validation"]["valid"].asBool() == false);
    REQUIRE_FALSE(body["validation"]["errors"].asArray().empty());
}

TEST_CASE("http server validate returns 400 for malformed JSON")
{
    HttpTestContext ctx;

    const auto res =
        ctx.client.Post("/v1/workflow-definitions/validate", "not json", "application/json");

    REQUIRE(res->status == 400);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "invalid-argument");
}

// -----------------------------------------------------------------------------
// Register
// -----------------------------------------------------------------------------

TEST_CASE("http server register returns 201 with the registered definition")
{
    HttpTestContext ctx;

    const auto res =
        ctx.client.Post("/v1/workflow-definitions", VALID_WORKFLOW_JSON, "application/json");

    REQUIRE(res->status == 201);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["definition"]["workflowName"].asString() == "orderProcessing");
    REQUIRE(body["definition"]["workflowVersion"].asInt() == 1);
    REQUIRE(body["definition"]["steps"].asArray().size() == 2);
}

TEST_CASE("http server register returns 400 for an invalid definition")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-definitions", R"json({"workflowName":""})json", "application/json"
    );

    REQUIRE(res->status == 400);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "invalid-argument");
}

// -----------------------------------------------------------------------------
// List
// -----------------------------------------------------------------------------

TEST_CASE("http server list returns empty array before any definitions are registered")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Get("/v1/workflow-definitions");

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["definitions"].asArray().empty());
}

TEST_CASE("http server list returns registered definitions")
{
    HttpTestContext ctx;
    ctx.registerDefinition();

    const auto res = ctx.client.Get("/v1/workflow-definitions");

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["definitions"].asArray().size() == 1);
    REQUIRE(body["definitions"][0]["workflowName"].asString() == "orderProcessing");
    REQUIRE(body["definitions"][0]["workflowVersion"].asInt() == 1);
}

// -----------------------------------------------------------------------------
// Start
// -----------------------------------------------------------------------------

TEST_CASE("http server start returns 201 with the new execution")
{
    HttpTestContext ctx;
    ctx.registerDefinition();

    const auto res = ctx.client.Post(
        "/v1/workflow-executions",
        R"json({"workflowName":"orderProcessing","workflowVersion":1})json", "application/json"
    );

    REQUIRE(res->status == 201);
    const auto body = workflow::json::parse(res->body);
    REQUIRE_FALSE(body["execution"]["workflowExecutionId"].asString().empty());
    REQUIRE(body["execution"]["status"].asString() == "Running");
    REQUIRE(body["execution"]["currentStepName"].asString() == "validateOrder");
}

TEST_CASE("http server start returns 404 when the definition is not registered")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-executions",
        R"json({"workflowName":"orderProcessing","workflowVersion":1})json", "application/json"
    );

    REQUIRE(res->status == 404);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "not-found");
}

TEST_CASE("http server start returns 400 for missing required fields")
{
    HttpTestContext ctx;

    const auto res =
        ctx.client.Post("/v1/workflow-executions", R"json({})json", "application/json");

    REQUIRE(res->status == 400);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "invalid-argument");
}

// -----------------------------------------------------------------------------
// Get
// -----------------------------------------------------------------------------

TEST_CASE("http server get returns the execution")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();

    const auto res = ctx.client.Get("/v1/workflow-executions/" + id);

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["execution"]["workflowExecutionId"].asString() == id);
    REQUIRE(body["execution"]["status"].asString() == "Running");
}

TEST_CASE("http server get returns 404 for an unknown execution id")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Get("/v1/workflow-executions/no-such-id");

    REQUIRE(res->status == 404);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "not-found");
}

// -----------------------------------------------------------------------------
// Cancel
// -----------------------------------------------------------------------------

TEST_CASE("http server cancel returns the canceled execution")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();

    const auto res = ctx.client.Delete("/v1/workflow-executions/" + id);

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["execution"]["status"].asString() == "Canceled");
}

TEST_CASE("http server cancel returns 404 for an unknown execution id")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Delete("/v1/workflow-executions/no-such-id");

    REQUIRE(res->status == 404);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "not-found");
}

TEST_CASE("http server cancel returns 409 when execution is not running")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.client.Delete("/v1/workflow-executions/" + id);

    const auto res = ctx.client.Delete("/v1/workflow-executions/" + id);

    REQUIRE(res->status == 409);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "conflict");
}

// -----------------------------------------------------------------------------
// Poll and claim
// -----------------------------------------------------------------------------

TEST_CASE("http server poll-and-claim returns a claimed step")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    ctx.startExecution();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/poll-and-claim",
        R"json({"workflowName":"orderProcessing","workflowVersion":1,"workerId":"worker-001","maxResults":1})json",
        "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["steps"].asArray().size() == 1);
    REQUIRE(body["steps"][0]["status"].asString() == "Running");
    REQUIRE(body["steps"][0]["workerId"].asString() == "worker-001");
    REQUIRE_FALSE(body["steps"][0]["leaseExpiresAt"].isNull());
}

TEST_CASE("http server poll-and-claim returns empty array when no steps are pending")
{
    HttpTestContext ctx;
    ctx.registerDefinition();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/poll-and-claim",
        R"json({"workflowName":"orderProcessing","workflowVersion":1,"workerId":"worker-001","maxResults":1})json",
        "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["steps"].asArray().empty());
}

TEST_CASE("http server poll-and-claim returns 400 for missing required fields")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/poll-and-claim", R"json({})json", "application/json"
    );

    REQUIRE(res->status == 400);
}

// -----------------------------------------------------------------------------
// Keep alive
// -----------------------------------------------------------------------------

TEST_CASE("http server keep-alive extends the lease")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/keep-alive",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-001\"}",
        "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["step"]["status"].asString() == "Running");
    REQUIRE_FALSE(body["step"]["leaseExpiresAt"].isNull());
}

TEST_CASE("http server keep-alive returns 409 for a non-owning worker")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/keep-alive",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-999\"}",
        "application/json"
    );

    REQUIRE(res->status == 409);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "conflict");
}

// -----------------------------------------------------------------------------
// Complete
// -----------------------------------------------------------------------------

TEST_CASE("http server complete transitions the execution to the next step")
{
    NextStepDecision decision;
    decision.nextStepName = "chargePayment";
    HttpTestContext ctx({decision});
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/complete",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-001\"}",
        "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["execution"]["status"].asString() == "Running");
    REQUIRE(body["execution"]["currentStepName"].asString() == "chargePayment");
}

TEST_CASE("http server complete returns 409 for a non-owning worker")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/complete",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-999\"}",
        "application/json"
    );

    REQUIRE(res->status == 409);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "conflict");
}

// -----------------------------------------------------------------------------
// Fail
// -----------------------------------------------------------------------------

TEST_CASE("http server fail retries when attempts remain")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/fail",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-001\",\"reason\":\"timeout\"}",
        "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["execution"]["status"].asString() == "Running");
}

TEST_CASE("http server fail returns 409 for a non-owning worker")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/fail",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-999\",\"reason\":\"timeout\"}",
        "application/json"
    );

    REQUIRE(res->status == 409);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "conflict");
}

TEST_CASE("http server fail returns 400 for a missing reason field")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.claimStep();

    const auto res = ctx.client.Post(
        "/v1/workflow-step-executions/fail",
        "{\"workflowExecutionId\":\"" + id +
            "\",\"stepName\":\"validateOrder\",\"workerId\":\"worker-001\"}",
        "application/json"
    );

    REQUIRE(res->status == 400);
    const auto body = workflow::json::parse(res->body);
    REQUIRE(body["type"].asString() == "invalid-argument");
}

// -----------------------------------------------------------------------------
// Response content types
// -----------------------------------------------------------------------------

TEST_CASE("http server sets application/json content type on success responses")
{
    HttpTestContext ctx;
    const auto res = ctx.client.Get("/v1/workflow-definitions");
    REQUIRE(res->get_header_value("Content-Type").find("application/json") != std::string::npos);
}

TEST_CASE("http server sets application/problem+json content type on error responses")
{
    HttpTestContext ctx;
    const auto res = ctx.client.Get("/v1/workflow-executions/no-such-id");
    REQUIRE(
        res->get_header_value("Content-Type").find("application/problem+json") != std::string::npos
    );
}
