#include "catch2/catch_amalgamated.hpp"
#include "httplib/httplib.h"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "mt/json.hpp"
#include "mt/json_parser.hpp"
#include "wf/http/workflow_http_server.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"

#include <memory>
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
            d.updatedState = mt::Json(mt::Json::Object{});
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
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
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
              database,
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
        return mt::JsonParser(res->body)
            .parse()
            .at("execution")
            .at("workflowExecutionId")
            .as_string();
    }

    std::string claimStep(const std::string& workerId = "worker-001")
    {
        const auto res = client.Post(
            "/v1/workflow-step-executions/poll-and-claim",
            R"json({"workflowName":"orderProcessing","workflowVersion":1,"workerId":"worker-001","maxResults":1})json",
            "application/json"
        );
        const auto body = mt::JsonParser(res->body).parse();
        return body.at("steps").as_array().at(0).at("workflowExecutionId").as_string();
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("validation").at("valid").as_bool() == true);
    REQUIRE(body.at("validation").at("errors").as_array().empty());
}

TEST_CASE("http server validate returns errors for an invalid definition")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-definitions/validate", R"json({"workflowName":""})json", "application/json"
    );

    REQUIRE(res->status == 200);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("validation").at("valid").as_bool() == false);
    REQUIRE_FALSE(body.at("validation").at("errors").as_array().empty());
}

TEST_CASE("http server validate returns 400 for malformed JSON")
{
    HttpTestContext ctx;

    const auto res =
        ctx.client.Post("/v1/workflow-definitions/validate", "not json", "application/json");

    REQUIRE(res->status == 400);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "invalid-argument");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("definition").at("workflowName").as_string() == "orderProcessing");
    REQUIRE(static_cast<int>(body.at("definition").at("workflowVersion").as_int64()) == 1);
    REQUIRE(body.at("definition").at("steps").as_array().size() == 2);
}

TEST_CASE("http server register returns 400 for an invalid definition")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-definitions", R"json({"workflowName":""})json", "application/json"
    );

    REQUIRE(res->status == 400);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "invalid-argument");
}

// -----------------------------------------------------------------------------
// List
// -----------------------------------------------------------------------------

TEST_CASE("http server list returns empty array before any definitions are registered")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Get("/v1/workflow-definitions");

    REQUIRE(res->status == 200);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("definitions").as_array().empty());
}

TEST_CASE("http server list returns registered definitions")
{
    HttpTestContext ctx;
    ctx.registerDefinition();

    const auto res = ctx.client.Get("/v1/workflow-definitions");

    REQUIRE(res->status == 200);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("definitions").as_array().size() == 1);
    REQUIRE(
        body.at("definitions").as_array().at(0).at("workflowName").as_string() == "orderProcessing"
    );
    REQUIRE(
        static_cast<int>(
            body.at("definitions").as_array().at(0).at("workflowVersion").as_int64()
        ) == 1
    );
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE_FALSE(body.at("execution").at("workflowExecutionId").as_string().empty());
    REQUIRE(body.at("execution").at("status").as_string() == "Running");
    REQUIRE(body.at("execution").at("currentStepName").as_string() == "validateOrder");
}

TEST_CASE("http server start returns 404 when the definition is not registered")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Post(
        "/v1/workflow-executions",
        R"json({"workflowName":"orderProcessing","workflowVersion":1})json", "application/json"
    );

    REQUIRE(res->status == 404);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "not-found");
}

TEST_CASE("http server start returns 400 for missing required fields")
{
    HttpTestContext ctx;

    const auto res =
        ctx.client.Post("/v1/workflow-executions", R"json({})json", "application/json");

    REQUIRE(res->status == 400);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "invalid-argument");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("execution").at("workflowExecutionId").as_string() == id);
    REQUIRE(body.at("execution").at("status").as_string() == "Running");
}

TEST_CASE("http server get returns 404 for an unknown execution id")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Get("/v1/workflow-executions/no-such-id");

    REQUIRE(res->status == 404);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "not-found");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("execution").at("status").as_string() == "Canceled");
}

TEST_CASE("http server cancel returns 404 for an unknown execution id")
{
    HttpTestContext ctx;

    const auto res = ctx.client.Delete("/v1/workflow-executions/no-such-id");

    REQUIRE(res->status == 404);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "not-found");
}

TEST_CASE("http server cancel returns 409 when execution is not running")
{
    HttpTestContext ctx;
    ctx.registerDefinition();
    const auto id = ctx.startExecution();
    ctx.client.Delete("/v1/workflow-executions/" + id);

    const auto res = ctx.client.Delete("/v1/workflow-executions/" + id);

    REQUIRE(res->status == 409);
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "conflict");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("steps").as_array().size() == 1);
    REQUIRE(body.at("steps").as_array().at(0).at("status").as_string() == "Running");
    REQUIRE(body.at("steps").as_array().at(0).at("workerId").as_string() == "worker-001");
    REQUIRE_FALSE(body.at("steps").as_array().at(0).at("leaseExpiresAt").is_null());
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("steps").as_array().empty());
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("step").at("status").as_string() == "Running");
    REQUIRE_FALSE(body.at("step").at("leaseExpiresAt").is_null());
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "conflict");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("execution").at("status").as_string() == "Running");
    REQUIRE(body.at("execution").at("currentStepName").as_string() == "chargePayment");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "conflict");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("execution").at("status").as_string() == "Running");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "conflict");
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
    const auto body = mt::JsonParser(res->body).parse();
    REQUIRE(body.at("type").as_string() == "invalid-argument");
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
