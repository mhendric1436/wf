#include "catch2/catch_amalgamated.hpp"
#include "tables/workflow_row_conversions.hpp"

#include <chrono>
#include <optional>
#include <string>

using workflow::StepExecutionStatus;
using workflow::WorkflowDefinition;
using workflow::WorkflowDefinitionRowMapping;
using workflow::WorkflowExecution;
using workflow::WorkflowExecutionRowMapping;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowStep;
using workflow::WorkflowStepExecution;
using workflow::WorkflowStepExecutionRowMapping;

TEST_CASE("workflow definition row conversion preserves steps and generated composite key")
{
    WorkflowDefinition definition;
    definition.workflowName = "orderProcessing";
    definition.workflowVersion = 7;
    definition.startWorkflowStepName = "validateOrder";
    definition.expectedExecutionTime = "PT10M";
    definition.steps = {
        WorkflowStep{
            .name = "validateOrder",
            .expectedExecutionTime = "PT30S",
            .maxRetries = 2,
            .additionalFields = {{"type", mt::Json("validation")}, {"required", mt::Json(true)}},
        },
        WorkflowStep{
            .name = "chargePayment",
            .expectedExecutionTime = "PT2M",
            .maxRetries = std::nullopt,
            .additionalFields = {{"service", mt::Json("payments")}},
        },
    };

    const auto row = workflow::toRow(definition);

    REQUIRE(WorkflowDefinitionRowMapping::key(row) == "orderProcessing:7");
    REQUIRE(row.workflowVersion == 7);
    REQUIRE(row.steps.size() == 2);
    REQUIRE(row.steps[0].additionalFields.at("type").as_string() == "validation");
    REQUIRE(row.steps[1].maxRetries == std::nullopt);

    const auto json = WorkflowDefinitionRowMapping::to_json(row);
    const auto decodedRow = WorkflowDefinitionRowMapping::from_json(json);
    const auto decoded = workflow::fromRow(decodedRow);

    REQUIRE(decoded.workflowName == definition.workflowName);
    REQUIRE(decoded.workflowVersion == definition.workflowVersion);
    REQUIRE(decoded.startWorkflowStepName == definition.startWorkflowStepName);
    REQUIRE(decoded.steps.size() == definition.steps.size());
    REQUIRE(decoded.steps[0].maxRetries == 2);
    REQUIRE(decoded.steps[0].additionalFields.at("required").as_bool());
    REQUIRE(decoded.steps[1].additionalFields.at("service").as_string() == "payments");
}

TEST_CASE("workflow execution row conversion preserves status timestamps and json state")
{
    const auto startedAt =
        std::chrono::system_clock::time_point{} + std::chrono::seconds{1710000000};
    const auto completedAt = startedAt + std::chrono::minutes{3};

    WorkflowExecution execution;
    execution.workflowExecutionId = "execution-001";
    execution.workflowName = "orderProcessing";
    execution.workflowVersion = 3;
    execution.status = WorkflowExecutionStatus::Completed;
    execution.currentStepName = "chargePayment";
    execution.input = mt::Json::object({{"orderId", mt::Json("order-123")}});
    execution.state = mt::Json::object({{"charged", mt::Json(true)}});
    execution.currentStepAttempt = 4;
    execution.failureReason = "ignored after completion";
    execution.startedAt = startedAt;
    execution.completedAt = completedAt;

    const auto row = workflow::toRow(execution);

    REQUIRE(WorkflowExecutionRowMapping::key(row) == "execution-001");
    REQUIRE(row.status == "Completed");
    REQUIRE(row.workflowVersion == 3);
    REQUIRE(row.currentStepAttempt == 4);
    REQUIRE(row.input.at("orderId").as_string() == "order-123");
    REQUIRE(row.state.at("charged").as_bool());
    REQUIRE(row.startedAt.has_value());
    REQUIRE(row.completedAt.has_value());

    const auto json = WorkflowExecutionRowMapping::to_json(row);
    const auto decoded = workflow::fromRow(WorkflowExecutionRowMapping::from_json(json));

    REQUIRE(decoded.workflowExecutionId == execution.workflowExecutionId);
    REQUIRE(decoded.status == WorkflowExecutionStatus::Completed);
    REQUIRE(decoded.input == execution.input);
    REQUIRE(decoded.state == execution.state);
    REQUIRE(decoded.currentStepAttempt == execution.currentStepAttempt);
    REQUIRE(decoded.failureReason == execution.failureReason);
    REQUIRE(decoded.startedAt == execution.startedAt);
    REQUIRE(decoded.completedAt == execution.completedAt);
}

TEST_CASE("workflow step execution row conversion preserves lease fields and composite key")
{
    const auto createdAt =
        std::chrono::system_clock::time_point{} + std::chrono::seconds{1710000100};
    const auto startedAt = createdAt + std::chrono::seconds{5};
    const auto leaseExpiresAt = startedAt + std::chrono::seconds{30};
    const auto completedAt = startedAt + std::chrono::seconds{10};

    WorkflowStepExecution step;
    step.workflowExecutionId = "execution-002";
    step.workflowName = "orderProcessing";
    step.workflowVersion = 5;
    step.stepName = "validateOrder";
    step.attempt = 2;
    step.status = StepExecutionStatus::Failed;
    step.workerId = "worker-007";
    step.leaseExpiresAt = leaseExpiresAt;
    step.failureReason = "validation timeout";
    step.createdAt = createdAt;
    step.startedAt = startedAt;
    step.completedAt = completedAt;
    step.input = mt::Json::object({{"orderId", mt::Json("order-456")}});
    step.state = mt::Json::object({{"attempted", mt::Json(true)}});
    step.output = mt::Json::object({{"errorCode", mt::Json("timeout")}});

    const auto row = workflow::toRow(step);

    REQUIRE(WorkflowStepExecutionRowMapping::key(row) == "execution-002:validateOrder:2");
    REQUIRE(row.status == "Failed");
    REQUIRE(row.workflowVersion == 5);
    REQUIRE(row.attempt == 2);
    REQUIRE(row.workerId == "worker-007");
    REQUIRE(row.leaseExpiresAt.has_value());
    REQUIRE(row.output.at("errorCode").as_string() == "timeout");

    const auto json = WorkflowStepExecutionRowMapping::to_json(row);
    const auto decoded = workflow::fromRow(WorkflowStepExecutionRowMapping::from_json(json));

    REQUIRE(decoded.workflowExecutionId == step.workflowExecutionId);
    REQUIRE(decoded.status == StepExecutionStatus::Failed);
    REQUIRE(decoded.workerId == step.workerId);
    REQUIRE(decoded.leaseExpiresAt == step.leaseExpiresAt);
    REQUIRE(decoded.failureReason == step.failureReason);
    REQUIRE(decoded.createdAt == step.createdAt);
    REQUIRE(decoded.startedAt == step.startedAt);
    REQUIRE(decoded.completedAt == step.completedAt);
    REQUIRE(decoded.input == step.input);
    REQUIRE(decoded.state == step.state);
    REQUIRE(decoded.output == step.output);
}
