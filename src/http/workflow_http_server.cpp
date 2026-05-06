#include "wf/http/workflow_http_server.hpp"

#include "httplib/httplib.h"
#include "wf/json.hpp"
#include "wf/store/workflow_definition_store.hpp"
#include "wf/workflow_definition.hpp"
#include "wf/workflow_execution.hpp"
#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>

namespace workflow::http
{

namespace
{

// -----------------------------------------------------------------------------
// Timestamp conversion
// -----------------------------------------------------------------------------

std::string toIso8601(std::chrono::system_clock::time_point tp)
{
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::tm tm{};
    gmtime_r(&tt, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    char result[40];
    std::snprintf(result, sizeof(result), "%s.%03ldZ", buf, static_cast<long>(ms.count()));
    return result;
}

[[maybe_unused]] std::chrono::system_clock::time_point fromIso8601(const std::string& s)
{
    std::tm tm{};
    int ms = 0;

    std::sscanf(
        s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
        &tm.tm_min, &tm.tm_sec, &ms
    );

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = 0;

    const auto tt = timegm(&tm);
    return std::chrono::system_clock::from_time_t(tt) + std::chrono::milliseconds(ms);
}

// -----------------------------------------------------------------------------
// Enum serialization
// -----------------------------------------------------------------------------

std::string toString(WorkflowExecutionStatus status)
{
    switch (status)
    {
    case WorkflowExecutionStatus::Running:
        return "Running";
    case WorkflowExecutionStatus::Completed:
        return "Completed";
    case WorkflowExecutionStatus::Failed:
        return "Failed";
    case WorkflowExecutionStatus::Canceled:
        return "Canceled";
    }
    return "Running";
}

std::string toString(StepExecutionStatus status)
{
    switch (status)
    {
    case StepExecutionStatus::Pending:
        return "Pending";
    case StepExecutionStatus::Running:
        return "Running";
    case StepExecutionStatus::Completed:
        return "Completed";
    case StepExecutionStatus::Failed:
        return "Failed";
    case StepExecutionStatus::Canceled:
        return "Canceled";
    }
    return "Pending";
}

// -----------------------------------------------------------------------------
// Domain type serialization
// -----------------------------------------------------------------------------

json::Value toJson(const WorkflowStep& step)
{
    json::Value::Object obj;
    obj["name"] = step.name;

    if (step.expectedExecutionTime.has_value())
    {
        obj["expectedExecutionTime"] = step.expectedExecutionTime.value();
    }

    if (step.maxRetries.has_value())
    {
        obj["maxRetries"] = step.maxRetries.value();
    }

    for (const auto& [k, v] : step.additionalFields)
    {
        obj[k] = v;
    }

    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowDefinition& def)
{
    json::Value::Array steps;
    for (const auto& step : def.steps)
    {
        steps.push_back(toJson(step));
    }

    json::Value::Object obj;
    obj["workflowName"] = def.workflowName;
    obj["workflowVersion"] = def.workflowVersion;
    obj["startWorkflowStepName"] = def.startWorkflowStepName;
    obj["expectedExecutionTime"] = def.expectedExecutionTime;
    obj["steps"] = json::Value(std::move(steps));
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowDefinitionKey& key)
{
    json::Value::Object obj;
    obj["workflowName"] = key.workflowName;
    obj["workflowVersion"] = key.workflowVersion;
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowExecution& exec)
{
    json::Value::Object obj;
    obj["workflowExecutionId"] = exec.workflowExecutionId;
    obj["workflowName"] = exec.workflowName;
    obj["workflowVersion"] = exec.workflowVersion;
    obj["status"] = toString(exec.status);
    obj["currentStepName"] = exec.currentStepName;
    obj["input"] = exec.input;
    obj["state"] = exec.state;
    obj["currentStepAttempt"] = exec.currentStepAttempt;
    obj["failureReason"] =
        exec.failureReason.has_value() ? json::Value(exec.failureReason.value()) : json::Value();
    obj["startedAt"] =
        exec.startedAt.has_value() ? json::Value(toIso8601(exec.startedAt.value())) : json::Value();
    obj["completedAt"] = exec.completedAt.has_value()
                             ? json::Value(toIso8601(exec.completedAt.value()))
                             : json::Value();
    return json::Value(std::move(obj));
}

json::Value toJson(const WorkflowStepExecution& step)
{
    json::Value::Object obj;
    obj["workflowExecutionId"] = step.workflowExecutionId;
    obj["workflowName"] = step.workflowName;
    obj["workflowVersion"] = step.workflowVersion;
    obj["stepName"] = step.stepName;
    obj["attempt"] = step.attempt;
    obj["status"] = toString(step.status);
    obj["workerId"] =
        step.workerId.has_value() ? json::Value(step.workerId.value()) : json::Value();
    obj["leaseExpiresAt"] = step.leaseExpiresAt.has_value()
                                ? json::Value(toIso8601(step.leaseExpiresAt.value()))
                                : json::Value();
    obj["failureReason"] =
        step.failureReason.has_value() ? json::Value(step.failureReason.value()) : json::Value();
    obj["createdAt"] =
        step.createdAt.has_value() ? json::Value(toIso8601(step.createdAt.value())) : json::Value();
    obj["startedAt"] =
        step.startedAt.has_value() ? json::Value(toIso8601(step.startedAt.value())) : json::Value();
    obj["completedAt"] = step.completedAt.has_value()
                             ? json::Value(toIso8601(step.completedAt.value()))
                             : json::Value();
    obj["input"] = step.input;
    obj["state"] = step.state;
    obj["output"] = step.output;
    return json::Value(std::move(obj));
}

json::Value toJson(const ValidationResult& result)
{
    json::Value::Array errors;
    for (const auto& e : result.errors)
    {
        errors.push_back(json::Value(e));
    }

    json::Value::Object obj;
    obj["valid"] = result.valid;
    obj["errors"] = json::Value(std::move(errors));
    return json::Value(std::move(obj));
}

// -----------------------------------------------------------------------------
// HTTP response helpers
// -----------------------------------------------------------------------------

void sendJson(
    httplib::Response& res,
    int status,
    const json::Value& body
)
{
    res.status = status;
    res.set_content(json::stringify(body), "application/json");
}

void sendProblem(
    httplib::Response& res,
    int status,
    const std::string& type,
    const std::string& title,
    const std::string& detail
)
{
    json::Value::Object obj;
    obj["type"] = type;
    obj["title"] = title;
    obj["status"] = status;
    obj["detail"] = detail;

    res.status = status;
    res.set_content(json::stringify(json::Value(std::move(obj))), "application/problem+json");
}

void handleException(httplib::Response& res)
{
    try
    {
        throw;
    }
    catch (const json::JsonParseError& e)
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

json::Value parseBody(const httplib::Request& req)
{
    return json::parse(req.body);
}

std::string requireString(
    const json::Value& obj,
    const std::string& key
)
{
    if (!obj.contains(key))
    {
        throw std::invalid_argument("missing required field: " + key);
    }
    return obj[key].asString();
}

int requireInt(
    const json::Value& obj,
    const std::string& key
)
{
    if (!obj.contains(key))
    {
        throw std::invalid_argument("missing required field: " + key);
    }
    return obj[key].asInt();
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

            json::Value::Object out;
            out["validation"] = toJson(response.validation);
            sendJson(res, 200, json::Value(std::move(out)));
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

            json::Value::Array defs;
            for (const auto& key : response.definitions)
            {
                defs.push_back(toJson(key));
            }

            json::Value::Object out;
            out["definitions"] = json::Value(std::move(defs));
            sendJson(res, 200, json::Value(std::move(out)));
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

            json::Value::Object out;
            out["definition"] = toJson(response.definition);
            sendJson(res, 201, json::Value(std::move(out)));
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
            request.input = body.contains("input") ? body["input"] : json::Value::object();

            const auto response = service.startWorkflowExecution(request);

            json::Value::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 201, json::Value(std::move(out)));
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

            json::Value::Object out;
            out["execution"] = toJson(response.execution.value());
            sendJson(res, 200, json::Value(std::move(out)));
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

            json::Value::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 200, json::Value(std::move(out)));
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
            request.maxResults = body.contains("maxResults")
                                     ? static_cast<std::size_t>(body["maxResults"].asInt())
                                     : 1;

            const auto response = service.pollAndClaimWorkflowSteps(request);

            json::Value::Array steps;
            for (const auto& step : response.steps)
            {
                steps.push_back(toJson(step));
            }

            json::Value::Object out;
            out["steps"] = json::Value(std::move(steps));
            sendJson(res, 200, json::Value(std::move(out)));
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

            json::Value::Object out;
            out["step"] = toJson(response.step);
            sendJson(res, 200, json::Value(std::move(out)));
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
            request.stepOutput =
                body.contains("stepOutput") ? body["stepOutput"] : json::Value::object();

            const auto response = service.completeWorkflowStep(request);

            json::Value::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 200, json::Value(std::move(out)));
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

            json::Value::Object out;
            out["execution"] = toJson(response.execution);
            sendJson(res, 200, json::Value(std::move(out)));
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
