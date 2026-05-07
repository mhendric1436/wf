#include "wf/workflow_orchestrator.hpp"

#include "tables/generated/workflow_definition_row.hpp"
#include "tables/generated/workflow_execution_row.hpp"
#include "tables/generated/workflow_step_execution_row.hpp"
#include "tables/workflow_row_conversions.hpp"
#include "wf/duration.hpp"
#include "wf/workflow_json.hpp"

#include "mt/query.hpp"
#include "mt/table.hpp"
#include "mt/transaction.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace workflow
{
namespace
{

using DefinitionTable = mt::Table<WorkflowDefinitionRow, WorkflowDefinitionRowMapping>;
using ExecutionTable = mt::Table<WorkflowExecutionRow, WorkflowExecutionRowMapping>;
using StepExecutionTable = mt::Table<WorkflowStepExecutionRow, WorkflowStepExecutionRowMapping>;

std::string definitionKey(
    const std::string& workflowName,
    int workflowVersion
)
{
    return workflowName + ":" + std::to_string(workflowVersion);
}

std::string stepExecutionKey(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    return workflowExecutionId + ":" + stepName + ":" + std::to_string(attempt);
}

std::string generateExecutionId()
{
    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist;

    const uint32_t a = dist(gen);
    const uint32_t b = dist(gen) & 0xFFFFu;
    const uint32_t c = (dist(gen) & 0x0FFFu) | 0x4000u;
    const uint32_t d = (dist(gen) & 0x3FFFu) | 0x8000u;
    const uint32_t e = dist(gen);
    const uint32_t f = dist(gen) & 0xFFFFu;

    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%08x%04x", a, b, c, d, e, f);
    return buf;
}

std::chrono::system_clock::time_point nowForStorage()
{
    return std::chrono::system_clock::now();
}

mt::QuerySpec stepQuery(
    const std::string& workflowName,
    int workflowVersion,
    StepExecutionStatus status
)
{
    mt::QuerySpec query;
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals,
            .path = "$.workflowName",
            .value = mt::Json(workflowName),
        }
    );
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals,
            .path = "$.workflowVersion",
            .value = mt::Json(workflowVersion),
        }
    );
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals,
            .path = "$.status",
            .value = mt::Json(toString(status)),
        }
    );
    return query;
}

mt::QuerySpec stepsByExecutionQuery(const std::string& workflowExecutionId)
{
    mt::QuerySpec query;
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals,
            .path = "$.workflowExecutionId",
            .value = mt::Json(workflowExecutionId),
        }
    );
    return query;
}

mt::QuerySpec expiredRunningStepsQuery()
{
    mt::QuerySpec query;
    query.predicates.push_back(
        mt::QueryPredicate{
            .op = mt::QueryOp::JsonEquals,
            .path = "$.status",
            .value = mt::Json(toString(StepExecutionStatus::Running)),
        }
    );
    return query;
}

const WorkflowStep& findStepDefinition(
    const WorkflowDefinition& definition,
    const std::string& stepName
)
{
    const auto iter = std::find_if(
        definition.steps.begin(), definition.steps.end(),
        [&stepName](const WorkflowStep& step) { return step.name == stepName; }
    );

    if (iter == definition.steps.end())
    {
        throw std::runtime_error("workflow step is not defined: " + stepName);
    }

    return *iter;
}

bool stepDefinitionExists(
    const WorkflowDefinition& definition,
    const std::string& stepName
)
{
    return std::any_of(
        definition.steps.begin(), definition.steps.end(),
        [&stepName](const WorkflowStep& step) { return step.name == stepName; }
    );
}

int maxRetriesForStep(const WorkflowStep& step)
{
    return step.maxRetries.value_or(0);
}

void validateWorkflowNameAndVersion(
    const std::string& workflowName,
    int workflowVersion
)
{
    if (workflowName.empty())
    {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (workflowVersion < 1)
    {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }
}

void validateExecutionId(const std::string& workflowExecutionId)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }
}

void validateWorkerId(const std::string& workerId)
{
    if (workerId.empty())
    {
        throw std::invalid_argument("workerId must not be empty");
    }
}

void validateStepName(const std::string& stepName)
{
    if (stepName.empty())
    {
        throw std::invalid_argument("stepName must not be empty");
    }
}

void validateExecutionIsRunning(const WorkflowExecution& execution)
{
    if (execution.status != WorkflowExecutionStatus::Running)
    {
        throw std::runtime_error(
            "workflow execution is not running: " + execution.workflowExecutionId
        );
    }
}

void validateClaimOwnership(
    const WorkflowStepExecution& stepExecution,
    const std::string& workerId
)
{
    const auto now = std::chrono::system_clock::now();

    if (stepExecution.status != StepExecutionStatus::Running)
    {
        throw std::runtime_error(
            "workflow step execution is not running: " + stepExecution.stepName
        );
    }

    if (!stepExecution.workerId.has_value())
    {
        throw std::runtime_error(
            "workflow step execution does not have a worker claim: " + stepExecution.stepName
        );
    }

    if (stepExecution.workerId.value() != workerId)
    {
        throw std::runtime_error(
            "workflow step execution is owned by a different worker: " + stepExecution.stepName
        );
    }

    if (!stepExecution.leaseExpiresAt.has_value())
    {
        throw std::runtime_error(
            "workflow step execution does not have an active lease: " + stepExecution.stepName
        );
    }

    if (stepExecution.leaseExpiresAt.value() <= now)
    {
        throw std::runtime_error(
            "workflow step execution lease has expired: " + stepExecution.stepName
        );
    }
}

std::chrono::seconds leaseDurationForStep(const WorkflowStep& step)
{
    if (!step.expectedExecutionTime.has_value())
    {
        throw std::runtime_error(
            "step expectedExecutionTime is required for lease calculation: " + step.name
        );
    }

    return calculateLeaseDuration(step.expectedExecutionTime.value());
}

WorkflowStepExecution makeStepExecution(
    const WorkflowExecution& execution,
    const std::string& stepName,
    int attempt,
    const mt::Json& input
)
{
    WorkflowStepExecution stepExecution;
    stepExecution.workflowExecutionId = execution.workflowExecutionId;
    stepExecution.workflowName = execution.workflowName;
    stepExecution.workflowVersion = execution.workflowVersion;
    stepExecution.stepName = stepName;
    stepExecution.attempt = attempt;
    stepExecution.status = StepExecutionStatus::Pending;
    stepExecution.input = input;
    stepExecution.state = execution.state;
    stepExecution.output = mt::Json(mt::Json::Object{});
    stepExecution.createdAt = nowForStorage();
    return stepExecution;
}

bool isClaimable(
    const WorkflowStepExecution& stepExecution,
    std::chrono::system_clock::time_point now
)
{
    if (stepExecution.status == StepExecutionStatus::Pending)
    {
        return true;
    }

    return stepExecution.status == StepExecutionStatus::Running &&
           stepExecution.leaseExpiresAt.has_value() && stepExecution.leaseExpiresAt.value() <= now;
}

} // namespace

struct WorkflowOrchestrator::Tables
{
    explicit Tables(mt::Database& database)
        : provider(database),
          transactions(database),
          definitions(provider.table<
                      WorkflowDefinitionRow,
                      WorkflowDefinitionRowMapping>()),
          executions(provider.table<
                     WorkflowExecutionRow,
                     WorkflowExecutionRowMapping>()),
          steps(provider.table<
                WorkflowStepExecutionRow,
                WorkflowStepExecutionRowMapping>())
    {
    }

    mt::TableProvider provider;
    mt::TransactionProvider transactions;
    DefinitionTable definitions;
    ExecutionTable executions;
    StepExecutionTable steps;
};

WorkflowOrchestrator::WorkflowOrchestrator(
    mt::Database& database,
    WorkflowLogic& workflowLogic
)
    : tables_(std::make_unique<Tables>(database)),
      workflowLogic_(workflowLogic)
{
}

WorkflowOrchestrator::~WorkflowOrchestrator() = default;

WorkflowDefinition
WorkflowOrchestrator::registerWorkflowDefinition(const WorkflowDefinition& definition)
{
    return tables_->transactions.retry([&](mt::Transaction& tx)
                                       { return registerWorkflowDefinition(tx, definition); });
}

WorkflowDefinition WorkflowOrchestrator::registerWorkflowDefinition(
    mt::Transaction& tx,
    const WorkflowDefinition& definition
)
{
    validateWorkflowNameAndVersion(definition.workflowName, definition.workflowVersion);

    tables_->definitions.put(tx, toRow(definition));
    return definition;
}

WorkflowExecution WorkflowOrchestrator::startWorkflow(
    const std::string& workflowName,
    int workflowVersion,
    const mt::Json& input
)
{
    return tables_->transactions.retry(
        [&](mt::Transaction& tx) { return startWorkflow(tx, workflowName, workflowVersion, input); }
    );
}

WorkflowExecution WorkflowOrchestrator::startWorkflow(
    mt::Transaction& tx,
    const std::string& workflowName,
    int workflowVersion,
    const mt::Json& input
)
{
    validateWorkflowNameAndVersion(workflowName, workflowVersion);

    const auto definition =
        tables_->definitions.get(tx, definitionKey(workflowName, workflowVersion));

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + workflowName);
    }

    const auto workflowDefinition = fromRow(*definition);

    WorkflowExecution execution;
    execution.workflowExecutionId = generateExecutionId();
    execution.workflowName = workflowName;
    execution.workflowVersion = workflowVersion;
    execution.status = WorkflowExecutionStatus::Running;
    execution.currentStepName = workflowDefinition.startWorkflowStepName;
    execution.input = input;
    execution.state = mt::Json(mt::Json::Object{});
    execution.currentStepAttempt = 0;
    execution.startedAt = nowForStorage();

    tables_->executions.put(tx, toRow(execution));
    tables_->steps.put(
        tx, toRow(makeStepExecution(
                execution, workflowDefinition.startWorkflowStepName, 0, execution.input
            ))
    );

    return execution;
}

std::vector<WorkflowStepExecution> WorkflowOrchestrator::pollAndClaimWorkflowSteps(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults
)
{
    return tables_->transactions.retry(
        [&](mt::Transaction& tx)
        {
            return pollAndClaimWorkflowSteps(
                tx, workflowName, workflowVersion, workerId, maxResults
            );
        }
    );
}

std::vector<WorkflowStepExecution> WorkflowOrchestrator::pollAndClaimWorkflowSteps(
    mt::Transaction& tx,
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults
)
{
    validateWorkflowNameAndVersion(workflowName, workflowVersion);
    validateWorkerId(workerId);

    if (maxResults == 0)
    {
        throw std::invalid_argument("maxResults must be greater than 0");
    }

    const auto definition =
        tables_->definitions.get(tx, definitionKey(workflowName, workflowVersion));

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + workflowName);
    }

    const auto workflowDefinition = fromRow(*definition);
    const auto now = nowForStorage();
    const auto pendingRows = tables_->steps.query(
        tx, stepQuery(workflowName, workflowVersion, StepExecutionStatus::Pending)
    );

    const auto runningRows = tables_->steps.query(
        tx, stepQuery(workflowName, workflowVersion, StepExecutionStatus::Running)
    );

    std::vector<WorkflowStepExecution> candidates;
    candidates.reserve(pendingRows.size() + runningRows.size());

    for (const auto& row : pendingRows)
    {
        candidates.push_back(fromRow(row));
    }

    for (const auto& row : runningRows)
    {
        candidates.push_back(fromRow(row));
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [](const WorkflowStepExecution& lhs, const WorkflowStepExecution& rhs)
        {
            if (lhs.createdAt != rhs.createdAt)
            {
                return lhs.createdAt < rhs.createdAt;
            }
            if (lhs.workflowExecutionId != rhs.workflowExecutionId)
            {
                return lhs.workflowExecutionId < rhs.workflowExecutionId;
            }
            if (lhs.stepName != rhs.stepName)
            {
                return lhs.stepName < rhs.stepName;
            }
            return lhs.attempt < rhs.attempt;
        }
    );

    std::vector<WorkflowStepExecution> claimed;
    claimed.reserve(maxResults);

    for (auto& candidate : candidates)
    {
        if (claimed.size() >= maxResults)
        {
            break;
        }

        auto current = tables_->steps.get(
            tx,
            stepExecutionKey(candidate.workflowExecutionId, candidate.stepName, candidate.attempt)
        );

        if (!current.has_value())
        {
            continue;
        }

        auto currentStep = fromRow(*current);

        if (!isClaimable(currentStep, now))
        {
            continue;
        }

        const auto& stepDefinition = findStepDefinition(workflowDefinition, currentStep.stepName);
        const auto leaseDuration = leaseDurationForStep(stepDefinition);

        currentStep.status = StepExecutionStatus::Running;
        currentStep.workerId = workerId;
        currentStep.leaseExpiresAt = now + leaseDuration;
        currentStep.failureReason.reset();
        currentStep.startedAt = now;

        tables_->steps.put(tx, toRow(currentStep));
        claimed.push_back(currentStep);
    }

    return claimed;
}

WorkflowStepExecution WorkflowOrchestrator::keepAliveStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId
)
{
    return tables_->transactions.retry(
        [&](mt::Transaction& tx)
        { return keepAliveStep(tx, workflowExecutionId, stepName, workerId); }
    );
}

WorkflowStepExecution WorkflowOrchestrator::keepAliveStep(
    mt::Transaction& tx,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId
)
{
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);
    validateWorkerId(workerId);

    auto execution = tables_->executions.get(tx, workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    auto workflowExecution = fromRow(*execution);
    validateExecutionIsRunning(workflowExecution);

    if (workflowExecution.currentStepName != stepName)
    {
        throw std::runtime_error("step does not match current workflow step: " + stepName);
    }

    auto definition = tables_->definitions.get(
        tx, definitionKey(workflowExecution.workflowName, workflowExecution.workflowVersion)
    );

    if (!definition.has_value())
    {
        throw std::runtime_error(
            "workflow definition not found: " + workflowExecution.workflowName
        );
    }

    auto stepExecution = tables_->steps.get(
        tx, stepExecutionKey(workflowExecutionId, stepName, workflowExecution.currentStepAttempt)
    );

    if (!stepExecution.has_value())
    {
        throw std::runtime_error("workflow step execution not found: " + stepName);
    }

    auto workflowStepExecution = fromRow(*stepExecution);
    validateClaimOwnership(workflowStepExecution, workerId);

    const auto workflowDefinition = fromRow(*definition);
    const auto& stepDefinition = findStepDefinition(workflowDefinition, stepName);
    workflowStepExecution.leaseExpiresAt = nowForStorage() + leaseDurationForStep(stepDefinition);

    tables_->steps.put(tx, toRow(workflowStepExecution));
    return workflowStepExecution;
}

WorkflowExecution WorkflowOrchestrator::completeStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId,
    const mt::Json& stepOutput
)
{
    return tables_->transactions.retry(
        [&](mt::Transaction& tx)
        { return completeStep(tx, workflowExecutionId, stepName, workerId, stepOutput); }
    );
}

WorkflowExecution WorkflowOrchestrator::completeStep(
    mt::Transaction& tx,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId,
    const mt::Json& stepOutput
)
{
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);
    validateWorkerId(workerId);

    auto execution = tables_->executions.get(tx, workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    WorkflowExecution updatedExecution = fromRow(*execution);
    validateExecutionIsRunning(updatedExecution);

    if (updatedExecution.currentStepName != stepName)
    {
        throw std::runtime_error("step does not match current workflow step: " + stepName);
    }

    auto stepExecution = tables_->steps.get(
        tx, stepExecutionKey(workflowExecutionId, stepName, updatedExecution.currentStepAttempt)
    );

    if (!stepExecution.has_value())
    {
        throw std::runtime_error("workflow step execution not found: " + stepName);
    }

    WorkflowStepExecution updatedStepExecution = fromRow(*stepExecution);
    validateClaimOwnership(updatedStepExecution, workerId);

    const auto definition = tables_->definitions.get(
        tx, definitionKey(updatedExecution.workflowName, updatedExecution.workflowVersion)
    );

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + updatedExecution.workflowName);
    }

    StepCompletionContext context;
    context.workflowName = updatedExecution.workflowName;
    context.workflowVersion = updatedExecution.workflowVersion;
    context.workflowExecutionId = updatedExecution.workflowExecutionId;
    context.completedStepName = stepName;
    context.input = updatedExecution.input;
    context.state = updatedExecution.state;
    context.stepOutput = stepOutput;

    const auto decision = workflowLogic_.decideNextStep(context);

    updatedStepExecution.status = StepExecutionStatus::Completed;
    updatedStepExecution.output = stepOutput;
    updatedStepExecution.leaseExpiresAt.reset();
    updatedStepExecution.completedAt = nowForStorage();
    tables_->steps.put(tx, toRow(updatedStepExecution));

    updatedExecution.state = decision.updatedState;

    if (decision.workflowComplete)
    {
        updatedExecution.status = WorkflowExecutionStatus::Completed;
        updatedExecution.completedAt = nowForStorage();
        tables_->executions.put(tx, toRow(updatedExecution));
        return updatedExecution;
    }

    if (!decision.nextStepName.has_value())
    {
        throw std::runtime_error(
            "next step decision must provide nextStepName or workflowComplete"
        );
    }

    const auto nextStepName = decision.nextStepName.value();

    const auto workflowDefinition = fromRow(*definition);

    if (!stepDefinitionExists(workflowDefinition, nextStepName))
    {
        throw std::runtime_error("next step is not defined: " + nextStepName);
    }

    updatedExecution.currentStepName = nextStepName;
    updatedExecution.currentStepAttempt = 0;
    tables_->executions.put(tx, toRow(updatedExecution));
    tables_->steps.put(
        tx, toRow(makeStepExecution(updatedExecution, nextStepName, 0, decision.nextStepInput))
    );

    return updatedExecution;
}

WorkflowExecution WorkflowOrchestrator::failStep(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId,
    const std::string& reason
)
{
    return tables_->transactions.retry(
        [&](mt::Transaction& tx)
        { return failStep(tx, workflowExecutionId, stepName, workerId, reason); }
    );
}

WorkflowExecution WorkflowOrchestrator::failStep(
    mt::Transaction& tx,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    const std::string& workerId,
    const std::string& reason
)
{
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);
    validateWorkerId(workerId);

    auto execution = tables_->executions.get(tx, workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    WorkflowExecution updatedExecution = fromRow(*execution);
    validateExecutionIsRunning(updatedExecution);

    if (updatedExecution.currentStepName != stepName)
    {
        throw std::runtime_error("step does not match current workflow step: " + stepName);
    }

    auto stepExecution = tables_->steps.get(
        tx, stepExecutionKey(workflowExecutionId, stepName, updatedExecution.currentStepAttempt)
    );

    if (!stepExecution.has_value())
    {
        throw std::runtime_error("workflow step execution not found: " + stepName);
    }

    WorkflowStepExecution updatedStepExecution = fromRow(*stepExecution);
    validateClaimOwnership(updatedStepExecution, workerId);

    const auto definition = tables_->definitions.get(
        tx, definitionKey(updatedExecution.workflowName, updatedExecution.workflowVersion)
    );

    if (!definition.has_value())
    {
        throw std::runtime_error("workflow definition not found: " + updatedExecution.workflowName);
    }

    const auto workflowDefinition = fromRow(*definition);
    const auto& stepDefinition = findStepDefinition(workflowDefinition, stepName);
    const int maxRetries = maxRetriesForStep(stepDefinition);

    updatedStepExecution.status = StepExecutionStatus::Failed;
    updatedStepExecution.failureReason = reason;
    updatedStepExecution.leaseExpiresAt.reset();
    updatedStepExecution.completedAt = nowForStorage();
    tables_->steps.put(tx, toRow(updatedStepExecution));

    if (updatedExecution.currentStepAttempt < maxRetries)
    {
        ++updatedExecution.currentStepAttempt;
        tables_->executions.put(tx, toRow(updatedExecution));
        tables_->steps.put(
            tx, toRow(makeStepExecution(
                    updatedExecution, stepName, updatedExecution.currentStepAttempt,
                    updatedStepExecution.input
                ))
        );

        return updatedExecution;
    }

    updatedExecution.status = WorkflowExecutionStatus::Failed;
    updatedExecution.failureReason = reason;
    updatedExecution.completedAt = nowForStorage();
    tables_->executions.put(tx, toRow(updatedExecution));

    return updatedExecution;
}

WorkflowExecution WorkflowOrchestrator::cancelWorkflow(const std::string& workflowExecutionId)
{
    return tables_->transactions.retry([&](mt::Transaction& tx)
                                       { return cancelWorkflow(tx, workflowExecutionId); });
}

WorkflowExecution WorkflowOrchestrator::cancelWorkflow(
    mt::Transaction& tx,
    const std::string& workflowExecutionId
)
{
    validateExecutionId(workflowExecutionId);

    auto execution = tables_->executions.get(tx, workflowExecutionId);

    if (!execution.has_value())
    {
        throw std::runtime_error("workflow execution not found: " + workflowExecutionId);
    }

    auto workflowExecution = fromRow(*execution);

    if (workflowExecution.status != WorkflowExecutionStatus::Running)
    {
        throw std::runtime_error("workflow execution is not running: " + workflowExecutionId);
    }

    const auto now = nowForStorage();
    const auto stepRows = tables_->steps.query(tx, stepsByExecutionQuery(workflowExecutionId));

    for (const auto& stepRow : stepRows)
    {
        auto step = fromRow(stepRow);

        if (step.status == StepExecutionStatus::Pending ||
            step.status == StepExecutionStatus::Running)
        {
            step.status = StepExecutionStatus::Canceled;
            step.workerId.reset();
            step.leaseExpiresAt.reset();
            step.completedAt = now;
            tables_->steps.put(tx, toRow(step));
        }
    }

    WorkflowExecution canceled = workflowExecution;
    canceled.status = WorkflowExecutionStatus::Canceled;
    canceled.completedAt = now;
    tables_->executions.put(tx, toRow(canceled));

    return canceled;
}

SweepResult WorkflowOrchestrator::sweepExpiredLeases()
{
    return tables_->transactions.retry([&](mt::Transaction& tx) { return sweepExpiredLeases(tx); });
}

SweepResult WorkflowOrchestrator::sweepExpiredLeases(mt::Transaction& tx)
{
    SweepResult sweepResult;
    const auto now = nowForStorage();
    const auto runningSteps = tables_->steps.query(tx, expiredRunningStepsQuery());

    for (const auto& expiredStepRow : runningSteps)
    {
        const auto expiredStep = fromRow(expiredStepRow);

        if (!expiredStep.leaseExpiresAt.has_value() || expiredStep.leaseExpiresAt.value() > now)
        {
            continue;
        }

        const auto execution = tables_->executions.get(tx, expiredStep.workflowExecutionId);

        if (!execution.has_value())
        {
            continue;
        }

        const auto workflowExecution = fromRow(*execution);

        if (workflowExecution.status != WorkflowExecutionStatus::Running)
        {
            continue;
        }

        if (workflowExecution.currentStepName != expiredStep.stepName ||
            workflowExecution.currentStepAttempt != expiredStep.attempt)
        {
            continue;
        }

        const auto definition = tables_->definitions.get(
            tx, definitionKey(workflowExecution.workflowName, workflowExecution.workflowVersion)
        );

        if (!definition.has_value())
        {
            continue;
        }

        const auto workflowDefinition = fromRow(*definition);
        const auto& stepDefinition = findStepDefinition(workflowDefinition, expiredStep.stepName);
        const int maxRetries = maxRetriesForStep(stepDefinition);

        WorkflowStepExecution failedStep = expiredStep;
        failedStep.status = StepExecutionStatus::Failed;
        failedStep.failureReason = "lease expired";
        failedStep.leaseExpiresAt.reset();
        failedStep.completedAt = now;
        tables_->steps.put(tx, toRow(failedStep));

        WorkflowExecution updatedExecution = workflowExecution;

        if (updatedExecution.currentStepAttempt < maxRetries)
        {
            ++updatedExecution.currentStepAttempt;
            tables_->executions.put(tx, toRow(updatedExecution));
            tables_->steps.put(
                tx, toRow(makeStepExecution(
                        updatedExecution, expiredStep.stepName, updatedExecution.currentStepAttempt,
                        expiredStep.input
                    ))
            );
            ++sweepResult.retriedCount;
        }
        else
        {
            updatedExecution.status = WorkflowExecutionStatus::Failed;
            updatedExecution.failureReason = "lease expired";
            updatedExecution.completedAt = now;
            tables_->executions.put(tx, toRow(updatedExecution));
            ++sweepResult.failedCount;
        }
    }

    return sweepResult;
}

std::optional<WorkflowExecution>
WorkflowOrchestrator::getWorkflowExecution(const std::string& workflowExecutionId) const
{
    validateExecutionId(workflowExecutionId);
    const auto row = tables_->executions.get(workflowExecutionId);
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return fromRow(*row);
}

std::optional<WorkflowExecution> WorkflowOrchestrator::getWorkflowExecution(
    mt::Transaction& tx,
    const std::string& workflowExecutionId
) const
{
    validateExecutionId(workflowExecutionId);
    const auto row = tables_->executions.get(tx, workflowExecutionId);
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return fromRow(*row);
}

WorkflowExecution WorkflowOrchestrator::putWorkflowExecution(const WorkflowExecution& execution)
{
    tables_->transactions.run([&](mt::Transaction& tx) { putWorkflowExecution(tx, execution); });
    return execution;
}

WorkflowExecution WorkflowOrchestrator::putWorkflowExecution(
    mt::Transaction& tx,
    const WorkflowExecution& execution
)
{
    validateExecutionId(execution.workflowExecutionId);

    tables_->executions.put(tx, toRow(execution));
    return execution;
}

std::optional<WorkflowStepExecution> WorkflowOrchestrator::getWorkflowStepExecution(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) const
{
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);

    if (attempt < 0)
    {
        throw std::invalid_argument("attempt must be greater than or equal to 0");
    }

    const auto row = tables_->steps.get(stepExecutionKey(workflowExecutionId, stepName, attempt));
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return fromRow(*row);
}

std::optional<WorkflowStepExecution> WorkflowOrchestrator::getWorkflowStepExecution(
    mt::Transaction& tx,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) const
{
    validateExecutionId(workflowExecutionId);
    validateStepName(stepName);

    if (attempt < 0)
    {
        throw std::invalid_argument("attempt must be greater than or equal to 0");
    }

    const auto row =
        tables_->steps.get(tx, stepExecutionKey(workflowExecutionId, stepName, attempt));
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return fromRow(*row);
}

WorkflowStepExecution
WorkflowOrchestrator::putWorkflowStepExecution(const WorkflowStepExecution& stepExecution)
{
    tables_->transactions.run([&](mt::Transaction& tx)
                              { putWorkflowStepExecution(tx, stepExecution); });
    return stepExecution;
}

WorkflowStepExecution WorkflowOrchestrator::putWorkflowStepExecution(
    mt::Transaction& tx,
    const WorkflowStepExecution& stepExecution
)
{
    validateExecutionId(stepExecution.workflowExecutionId);
    validateStepName(stepExecution.stepName);

    if (stepExecution.attempt < 0)
    {
        throw std::invalid_argument("attempt must be greater than or equal to 0");
    }

    tables_->steps.put(tx, toRow(stepExecution));
    return stepExecution;
}

std::vector<WorkflowDefinitionKey> WorkflowOrchestrator::listWorkflowDefinitions() const
{
    const auto definitions = tables_->definitions.list();

    std::vector<WorkflowDefinitionKey> keys;
    keys.reserve(definitions.size());

    for (const auto& definition : definitions)
    {
        keys.push_back(
            WorkflowDefinitionKey{
                .workflowName = definition.workflowName,
                .workflowVersion = static_cast<int>(definition.workflowVersion),
            }
        );
    }

    return keys;
}

std::vector<WorkflowDefinitionKey>
WorkflowOrchestrator::listWorkflowDefinitions(mt::Transaction& tx) const
{
    const auto definitions = tables_->definitions.list(tx);

    std::vector<WorkflowDefinitionKey> keys;
    keys.reserve(definitions.size());

    for (const auto& definition : definitions)
    {
        keys.push_back(
            WorkflowDefinitionKey{
                .workflowName = definition.workflowName,
                .workflowVersion = static_cast<int>(definition.workflowVersion),
            }
        );
    }

    return keys;
}

std::optional<WorkflowDefinition> WorkflowOrchestrator::getWorkflowDefinition(
    const std::string& workflowName,
    int workflowVersion
) const
{
    validateWorkflowNameAndVersion(workflowName, workflowVersion);
    const auto row = tables_->definitions.get(definitionKey(workflowName, workflowVersion));
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return fromRow(*row);
}

std::optional<WorkflowDefinition> WorkflowOrchestrator::getWorkflowDefinition(
    mt::Transaction& tx,
    const std::string& workflowName,
    int workflowVersion
) const
{
    validateWorkflowNameAndVersion(workflowName, workflowVersion);
    const auto row = tables_->definitions.get(tx, definitionKey(workflowName, workflowVersion));
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return fromRow(*row);
}

} // namespace workflow
