#include "wf/http/workflow_http_server.hpp"

#include "httplib/httplib.h"
#include "mt/errors.hpp"
#include "mt/json.hpp"
#include "mt/json_parser.hpp"
#include "wf/workflow_json.hpp"

#include <stdexcept>
#include <string>

namespace workflow::http
{

namespace
{

// -----------------------------------------------------------------------------
// HTTP response helpers
// -----------------------------------------------------------------------------

void sendJson(
    httplib::Response& res,
    int status,
    const mt::Json& body
)
{
    res.status = status;
    res.set_content(body.canonical_string(), "application/json");
}

void sendProblem(
    httplib::Response& res,
    int status,
    const std::string& type,
    const std::string& title,
    const std::string& detail
)
{
    mt::Json::Object obj;
    obj["type"] = type;
    obj["title"] = title;
    obj["status"] = status;
    obj["detail"] = detail;

    res.status = status;
    res.set_content(mt::Json(std::move(obj)).canonical_string(), "application/problem+json");
}

void handleException(httplib::Response& res)
{
    try
    {
        throw;
    }
    catch (const mt::BackendError& e)
    {
        sendProblem(res, 400, "invalid-argument", "Invalid Argument", e.what());
    }
    catch (const std::invalid_argument& e)
    {
        sendProblem(res, 400, "invalid-argument", "Invalid Argument", e.what());
    }
    catch (const std::runtime_error& e)
    {
        const std::string msg = e.what();
        if (msg.find("not found") != std::string::npos)
        {
            sendProblem(res, 404, "not-found", "Not Found", msg);
        }
        else
        {
            sendProblem(res, 409, "conflict", "Conflict", msg);
        }
    }
    catch (const std::exception& e)
    {
        sendProblem(res, 500, "internal-error", "Internal Server Error", e.what());
    }
}

mt::Json parseBody(const httplib::Request& req)
{
    return mt::JsonParser(req.body).parse();
}

std::string requireString(
    const mt::Json& obj,
    const std::string& key
)
{
    if (!obj.is_object() || !obj.as_object().count(key))
    {
        throw std::invalid_argument("missing required field: " + key);
    }
    return obj[key].as_string();
}

int requireInt(
    const mt::Json& obj,
    const std::string& key
)
{
    if (!obj.is_object() || !obj.as_object().count(key))
    {
        throw std::invalid_argument("missing required field: " + key);
    }
    return static_cast<int>(obj[key].as_int64());
}

} // namespace

// -----------------------------------------------------------------------------
// Impl
// -----------------------------------------------------------------------------

struct WorkflowHttpServer::Impl
{
    WorkflowService& service;
    int port;
    bool bound = false;
    httplib::Server svr;

    explicit Impl(
        WorkflowService& service,
        int port
    )
        : service(service),
          port(port)
    {
        registerRoutes();
    }

    int bind()
    {
        int actual;
        if (port == 0)
        {
            actual = svr.bind_to_any_port("0.0.0.0");
        }
        else
        {
            actual = svr.bind_to_port("0.0.0.0", port) ? port : -1;
        }

        if (actual < 0)
        {
            throw std::runtime_error("failed to bind server on port " + std::to_string(port));
        }

        bound = true;
        return actual;
    }

    void registerRoutes()
    {
        svr.Post(
            "/v1/workflow-definitions/validate",
            [this](const httplib::Request& req, httplib::Response& res)
            { handleValidate(req, res); }
        );

        svr.Get(
            "/v1/workflow-definitions",
            [this](const httplib::Request& req, httplib::Response& res) { handleList(req, res); }
        );

        svr.Post(
            "/v1/workflow-definitions", [this](const httplib::Request& req, httplib::Response& res)
            { handleRegister(req, res); }
        );

        svr.Post(
            "/v1/workflow-executions",
            [this](const httplib::Request& req, httplib::Response& res) { handleStart(req, res); }
        );

        svr.Get(
            "/v1/workflow-executions/:id",
            [this](const httplib::Request& req, httplib::Response& res) { handleGet(req, res); }
        );

        svr.Delete(
            "/v1/workflow-executions/:id",
            [this](const httplib::Request& req, httplib::Response& res) { handleCancel(req, res); }
        );

        svr.Post(
            "/v1/workflow-step-executions/poll-and-claim",
            [this](const httplib::Request& req, httplib::Response& res)
            { handlePollAndClaim(req, res); }
        );

        svr.Post(
            "/v1/workflow-step-executions/keep-alive",
            [this](const httplib::Request& req, httplib::Response& res)
            { handleKeepAlive(req, res); }
        );

        svr.Post(
            "/v1/workflow-step-executions/complete",
            [this](const httplib::Request& req, httplib::Response& res)
            { handleComplete(req, res); }
        );

        svr.Post(
            "/v1/workflow-step-executions/fail",
            [this](const httplib::Request& req, httplib::Response& res) { handleFail(req, res); }
        );
    }

    void handleValidate(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);
            const auto response = service.validateWorkflowDefinition(
                ValidateWorkflowDefinitionRequest{.definitionJson = body}
            );

            mt::Json::Object out;
            out["validation"] = toJson(response.validation);
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleList(
        const httplib::Request&,
        httplib::Response& res
    )
    {
        try
        {
            const auto response = service.listWorkflowDefinitions(ListWorkflowDefinitionsRequest{});

            mt::Json::Array defs;
            for (const auto& key : response.definitions)
            {
                defs.push_back(toJson(key));
            }

            mt::Json::Object out;
            out["definitions"] = mt::Json(std::move(defs));
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleRegister(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);
            const auto response = service.registerWorkflowDefinition(
                RegisterWorkflowDefinitionRequest{.definitionJson = body}
            );

            mt::Json::Object out;
            out["definition"] = toJson(response.definition);
            sendJson(res, 201, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleStart(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);

            StartWorkflowExecutionRequest request;
            request.workflowName = requireString(body, "workflowName");
            request.workflowVersion = requireInt(body, "workflowVersion");
            request.input = (body.is_object() && body.as_object().count("input"))
                                ? body["input"]
                                : mt::Json(mt::Json::Object{});

            const auto response = service.startWorkflowExecution(request);

            mt::Json::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 201, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleGet(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto id = req.path_params.at("id");
            const auto response = service.getWorkflowExecution(
                GetWorkflowExecutionRequest{.workflowExecutionId = id}
            );

            if (!response.execution.has_value())
            {
                sendProblem(
                    res, 404, "not-found", "Not Found", "workflow execution not found: " + id
                );
                return;
            }

            mt::Json::Object out;
            out["execution"] = toJson(response.execution.value());
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleCancel(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto id = req.path_params.at("id");
            const auto response =
                service.cancelWorkflow(CancelWorkflowRequest{.workflowExecutionId = id});

            mt::Json::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handlePollAndClaim(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);

            PollAndClaimWorkflowStepsRequest request;
            request.workflowName = requireString(body, "workflowName");
            request.workflowVersion = requireInt(body, "workflowVersion");
            request.workerId = requireString(body, "workerId");
            request.maxResults =
                (body.is_object() && body.as_object().count("maxResults"))
                    ? static_cast<std::size_t>(static_cast<int>(body["maxResults"].as_int64()))
                    : 1;

            const auto response = service.pollAndClaimWorkflowSteps(request);

            mt::Json::Array steps;
            for (const auto& step : response.steps)
            {
                steps.push_back(toJson(step));
            }

            mt::Json::Object out;
            out["steps"] = mt::Json(std::move(steps));
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleKeepAlive(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);

            KeepAliveWorkflowStepRequest request;
            request.workflowExecutionId = requireString(body, "workflowExecutionId");
            request.stepName = requireString(body, "stepName");
            request.workerId = requireString(body, "workerId");

            const auto response = service.keepAliveWorkflowStep(request);

            mt::Json::Object out;
            out["step"] = toJson(response.step);
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleComplete(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);

            CompleteWorkflowStepRequest request;
            request.workflowExecutionId = requireString(body, "workflowExecutionId");
            request.stepName = requireString(body, "stepName");
            request.workerId = requireString(body, "workerId");
            request.stepOutput = (body.is_object() && body.as_object().count("stepOutput"))
                                     ? body["stepOutput"]
                                     : mt::Json(mt::Json::Object{});

            const auto response = service.completeWorkflowStep(request);

            mt::Json::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }

    void handleFail(
        const httplib::Request& req,
        httplib::Response& res
    )
    {
        try
        {
            const auto body = parseBody(req);

            FailWorkflowStepRequest request;
            request.workflowExecutionId = requireString(body, "workflowExecutionId");
            request.stepName = requireString(body, "stepName");
            request.workerId = requireString(body, "workerId");
            request.reason = requireString(body, "reason");

            const auto response = service.failWorkflowStep(request);

            mt::Json::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 200, mt::Json(std::move(out)));
        }
        catch (...)
        {
            handleException(res);
        }
    }
};

// -----------------------------------------------------------------------------
// WorkflowHttpServer
// -----------------------------------------------------------------------------

WorkflowHttpServer::WorkflowHttpServer(
    WorkflowService& service,
    int port
)
    : impl_(
          std::make_unique<Impl>(
              service,
              port
          )
      )
{
}

WorkflowHttpServer::~WorkflowHttpServer() = default;

int WorkflowHttpServer::bind()
{
    return impl_->bind();
}

void WorkflowHttpServer::start()
{
    if (impl_->bound)
    {
        impl_->svr.listen_after_bind();
    }
    else
    {
        impl_->svr.listen("0.0.0.0", impl_->port);
    }
}

void WorkflowHttpServer::stop()
{
    impl_->svr.stop();
}

} // namespace workflow::http
