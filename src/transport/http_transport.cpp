#include "wf/transport/http_transport.hpp"

#include "httplib/httplib.h"
#include "mt/json.hpp"
#include "mt/json_parser.hpp"
#include "wf/duration.hpp"
#include "wf/workflow_json.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

namespace workflow::transport
{

namespace
{

std::string toIso8601Duration(std::chrono::seconds value)
{
    return "PT" + std::to_string(value.count()) + "S";
}

void checkResponse(const httplib::Result& result)
{
    if (!result)
    {
        throw std::runtime_error("HTTP request failed: no response from server");
    }

    if (result->status >= 200 && result->status < 300)
    {
        return;
    }

    std::string detail = result->body;

    try
    {
        const auto body = mt::JsonParser(result->body).parse();
        if (body.is_object() && body.as_object().count("detail"))
        {
            detail = body.at("detail").as_string();
        }
    }
    catch (...)
    {
    }

    if (result->status == 400)
    {
        throw std::invalid_argument(detail);
    }

    throw std::runtime_error(detail);
}

mt::Json parseBody(const httplib::Result& result)
{
    return mt::JsonParser(result->body).parse();
}

} // namespace

struct HttpTransport::Impl
{
    httplib::Client client;

    Impl(
        const std::string& host,
        int port
    )
        : client(
              host,
              port
          )
    {
    }

    httplib::Result post(
        const std::string& path,
        const mt::Json& body
    )
    {
        return client.Post(path, body.canonical_string(), "application/json");
    }

    httplib::Result get(const std::string& path)
    {
        return client.Get(path);
    }

    httplib::Result del(const std::string& path)
    {
        return client.Delete(path);
    }
};

HttpTransport::HttpTransport(
    const std::string& host,
    int port
)
    : impl_(
          std::make_unique<Impl>(
              host,
              port
          )
      )
{
}

HttpTransport::~HttpTransport() = default;

ValidateWorkflowDefinitionResponse
HttpTransport::validateWorkflowDefinition(const ValidateWorkflowDefinitionRequest& request)
{
    const auto result = impl_->post("/v1/workflow-definitions/validate", request.definitionJson);
    checkResponse(result);
    const auto body = parseBody(result);
    return ValidateWorkflowDefinitionResponse{
        .validation = validationResultFromJson(body.at("validation")),
    };
}

RegisterWorkflowDefinitionResponse
HttpTransport::registerWorkflowDefinition(const RegisterWorkflowDefinitionRequest& request)
{
    const auto result = impl_->post("/v1/workflow-definitions", request.definitionJson);
    checkResponse(result);
    const auto body = parseBody(result);
    return RegisterWorkflowDefinitionResponse{
        .definition = parseWorkflowDefinition(body.at("definition")),
    };
}

ListWorkflowDefinitionsResponse
HttpTransport::listWorkflowDefinitions(const ListWorkflowDefinitionsRequest&)
{
    const auto result = impl_->get("/v1/workflow-definitions");
    checkResponse(result);
    const auto body = parseBody(result);

    ListWorkflowDefinitionsResponse response;
    for (const auto& item : body.at("definitions").as_array())
    {
        response.definitions.push_back(workflowDefinitionKeyFromJson(item));
    }
    return response;
}

StartWorkflowExecutionResponse
HttpTransport::startWorkflowExecution(const StartWorkflowExecutionRequest& request)
{
    mt::Json::Object obj;
    obj["workflowName"] = request.workflowName;
    obj["workflowVersion"] = request.workflowVersion;
    obj["input"] = request.input;

    const auto result = impl_->post("/v1/workflow-executions", mt::Json(std::move(obj)));
    checkResponse(result);
    const auto body = parseBody(result);
    return StartWorkflowExecutionResponse{
        .execution = workflowExecutionFromJson(body.at("execution")),
    };
}

GetWorkflowExecutionResponse
HttpTransport::getWorkflowExecution(const GetWorkflowExecutionRequest& request)
{
    const auto result = impl_->get("/v1/workflow-executions/" + request.workflowExecutionId);

    if (!result)
    {
        throw std::runtime_error("HTTP request failed: no response from server");
    }

    if (result->status == 404)
    {
        return GetWorkflowExecutionResponse{.execution = std::nullopt};
    }

    checkResponse(result);
    const auto body = parseBody(result);
    return GetWorkflowExecutionResponse{
        .execution = workflowExecutionFromJson(body.at("execution")),
    };
}

CancelWorkflowResponse HttpTransport::cancelWorkflow(const CancelWorkflowRequest& request)
{
    const auto result = impl_->del("/v1/workflow-executions/" + request.workflowExecutionId);
    checkResponse(result);
    const auto body = parseBody(result);
    return CancelWorkflowResponse{
        .execution = workflowExecutionFromJson(body.at("execution")),
    };
}

PollAndClaimWorkflowStepsResponse
HttpTransport::pollAndClaimWorkflowSteps(const PollAndClaimWorkflowStepsRequest& request)
{
    mt::Json::Object obj;
    obj["workflowName"] = request.workflowName;
    obj["workflowVersion"] = request.workflowVersion;
    obj["workerId"] = request.workerId;
    obj["maxResults"] = static_cast<int>(request.maxResults);

    const auto result =
        impl_->post("/v1/workflow-step-executions/poll-and-claim", mt::Json(std::move(obj)));
    checkResponse(result);
    const auto body = parseBody(result);

    PollAndClaimWorkflowStepsResponse response;
    for (const auto& item : body.at("steps").as_array())
    {
        response.steps.push_back(workflowStepExecutionFromJson(item));
    }
    return response;
}

KeepAliveWorkflowStepResponse
HttpTransport::keepAliveWorkflowStep(const KeepAliveWorkflowStepRequest& request)
{
    mt::Json::Object obj;
    obj["workflowExecutionId"] = request.workflowExecutionId;
    obj["stepName"] = request.stepName;
    obj["workerId"] = request.workerId;

    const auto result =
        impl_->post("/v1/workflow-step-executions/keep-alive", mt::Json(std::move(obj)));
    checkResponse(result);
    const auto body = parseBody(result);
    return KeepAliveWorkflowStepResponse{
        .step = workflowStepExecutionFromJson(body.at("step")),
    };
}

CompleteWorkflowStepResponse
HttpTransport::completeWorkflowStep(const CompleteWorkflowStepRequest& request)
{
    mt::Json::Object obj;
    obj["workflowExecutionId"] = request.workflowExecutionId;
    obj["stepName"] = request.stepName;
    obj["workerId"] = request.workerId;
    obj["stepOutput"] = request.stepOutput;
    if (request.nextStepDelay.has_value())
    {
        obj["nextStepDelay"] = toIso8601Duration(request.nextStepDelay.value());
    }

    const auto result =
        impl_->post("/v1/workflow-step-executions/complete", mt::Json(std::move(obj)));
    checkResponse(result);
    const auto body = parseBody(result);
    return CompleteWorkflowStepResponse{
        .execution = workflowExecutionFromJson(body.at("execution")),
    };
}

FailWorkflowStepResponse HttpTransport::failWorkflowStep(const FailWorkflowStepRequest& request)
{
    mt::Json::Object obj;
    obj["workflowExecutionId"] = request.workflowExecutionId;
    obj["stepName"] = request.stepName;
    obj["workerId"] = request.workerId;
    obj["reason"] = request.reason;

    const auto result = impl_->post("/v1/workflow-step-executions/fail", mt::Json(std::move(obj)));
    checkResponse(result);
    const auto body = parseBody(result);
    return FailWorkflowStepResponse{
        .execution = workflowExecutionFromJson(body.at("execution")),
    };
}

} // namespace workflow::transport
