#include "catch2/catch_amalgamated.hpp"
#include "mt/backend.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "wf/workflow_logic.hpp"
#include "wf/workflow_orchestrator.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using workflow::NextStepDecision;
using workflow::StepCompletionContext;
using workflow::StepExecutionStatus;
using workflow::WorkflowDefinition;
using workflow::WorkflowExecution;
using workflow::WorkflowExecutionStatus;
using workflow::WorkflowLogic;
using workflow::WorkflowOrchestrator;
using workflow::WorkflowStep;
using workflow::WorkflowStepExecution;

namespace
{

WorkflowDefinition makeWorkflowDefinition(int validateOrderMaxRetries = 2)
{
    WorkflowDefinition definition;
    definition.workflowName = "orderProcessing";
    definition.workflowVersion = 1;
    definition.startWorkflowStepName = "validateOrder";
    definition.expectedExecutionTime = "PT10M";
    definition.steps = {
        WorkflowStep{
            .name = "validateOrder",
            .expectedExecutionTime = "PT30S",
            .maxRetries = validateOrderMaxRetries,
        },
        WorkflowStep{
            .name = "chargePayment",
            .expectedExecutionTime = "PT2M",
            .maxRetries = 1,
        },
    };
    return definition;
}

NextStepDecision completeWorkflowDecision()
{
    NextStepDecision decision;
    decision.workflowComplete = true;
    decision.updatedState = mt::Json(mt::Json::Object{});
    return decision;
}

NextStepDecision nextStepDecision(const std::string& stepName)
{
    NextStepDecision decision;
    decision.workflowComplete = false;
    decision.nextStepName = stepName;
    decision.updatedState = mt::Json(mt::Json::Object{});
    return decision;
}

class ScriptedWorkflowLogic final : public WorkflowLogic
{
  public:
    explicit ScriptedWorkflowLogic(std::vector<NextStepDecision> decisions = {})
        : decisions_(std::move(decisions))
    {
    }

    NextStepDecision decideNextStep(const StepCompletionContext& context) override
    {
        lastContext = context;
        ++callCount;

        if (onDecide)
        {
            onDecide(context, callCount);
        }

        if (nextDecisionIndex_ >= decisions_.size())
        {
            return completeWorkflowDecision();
        }

        return decisions_.at(nextDecisionIndex_++);
    }

    int callCount = 0;
    std::optional<StepCompletionContext> lastContext;
    std::function<void(const StepCompletionContext&, int)> onDecide;

  private:
    std::vector<NextStepDecision> decisions_;
    std::size_t nextDecisionIndex_ = 0;
};

struct ConflictInjectionState
{
    std::function<void()> onReadSnapshot;
    std::function<void()> onQuerySnapshot;
    int readSnapshotInjections = 0;
    int querySnapshotInjections = 0;
};

class ConflictInjectingSession final : public mt::IBackendSession
{
  public:
    ConflictInjectingSession(
        std::unique_ptr<mt::IBackendSession> inner,
        std::shared_ptr<ConflictInjectionState> state
    )
        : inner_(std::move(inner)),
          state_(std::move(state))
    {
    }

    void begin_backend_transaction() override
    {
        inner_->begin_backend_transaction();
    }

    void commit_backend_transaction() override
    {
        inner_->commit_backend_transaction();
    }

    void abort_backend_transaction() noexcept override
    {
        inner_->abort_backend_transaction();
    }

    mt::Version read_clock() override
    {
        return inner_->read_clock();
    }

    mt::Version lock_clock_and_read() override
    {
        return inner_->lock_clock_and_read();
    }

    mt::Version increment_clock_and_return() override
    {
        return inner_->increment_clock_and_return();
    }

    mt::TxId create_transaction_id() override
    {
        return inner_->create_transaction_id();
    }

    void register_active_transaction(
        mt::TxId txId,
        mt::Version version
    ) override
    {
        inner_->register_active_transaction(std::move(txId), version);
    }

    void unregister_active_transaction(mt::TxId txId) noexcept override
    {
        inner_->unregister_active_transaction(std::move(txId));
    }

    std::optional<mt::DocumentEnvelope> read_snapshot(
        mt::CollectionId collection,
        std::string_view key,
        mt::Version version
    ) override
    {
        auto result = inner_->read_snapshot(collection, key, version);
        injectReadSnapshotConflict();
        return result;
    }

    std::optional<mt::DocumentMetadata> read_current_metadata(
        mt::CollectionId collection,
        std::string_view key
    ) override
    {
        return inner_->read_current_metadata(collection, key);
    }

    mt::QueryResultEnvelope query_snapshot(
        mt::CollectionId collection,
        const mt::QuerySpec& query,
        mt::Version version
    ) override
    {
        auto result = inner_->query_snapshot(collection, query, version);
        injectQuerySnapshotConflict();
        return result;
    }

    mt::QueryMetadataResult query_current_metadata(
        mt::CollectionId collection,
        const mt::QuerySpec& query
    ) override
    {
        return inner_->query_current_metadata(collection, query);
    }

    mt::QueryResultEnvelope list_snapshot(
        mt::CollectionId collection,
        const mt::ListOptions& options,
        mt::Version version
    ) override
    {
        return inner_->list_snapshot(collection, options, version);
    }

    mt::QueryMetadataResult list_current_metadata(
        mt::CollectionId collection,
        const mt::ListOptions& options
    ) override
    {
        return inner_->list_current_metadata(collection, options);
    }

    void insert_history(
        mt::CollectionId collection,
        const mt::WriteEnvelope& write,
        mt::Version commitVersion
    ) override
    {
        inner_->insert_history(collection, write, commitVersion);
    }

    void upsert_current(
        mt::CollectionId collection,
        const mt::WriteEnvelope& write,
        mt::Version commitVersion
    ) override
    {
        inner_->upsert_current(collection, write, commitVersion);
    }

  private:
    void injectReadSnapshotConflict()
    {
        if (!state_->onReadSnapshot)
        {
            return;
        }

        auto callback = std::move(state_->onReadSnapshot);
        state_->onReadSnapshot = nullptr;
        ++state_->readSnapshotInjections;
        callback();
    }

    void injectQuerySnapshotConflict()
    {
        if (!state_->onQuerySnapshot)
        {
            return;
        }

        auto callback = std::move(state_->onQuerySnapshot);
        state_->onQuerySnapshot = nullptr;
        ++state_->querySnapshotInjections;
        callback();
    }

    std::unique_ptr<mt::IBackendSession> inner_;
    std::shared_ptr<ConflictInjectionState> state_;
};

class ConflictInjectingBackend final : public mt::IDatabaseBackend
{
  public:
    std::shared_ptr<ConflictInjectionState> state = std::make_shared<ConflictInjectionState>();

    mt::BackendCapabilities capabilities() const override
    {
        return inner_.capabilities();
    }

    std::unique_ptr<mt::IBackendSession> open_session() override
    {
        return std::make_unique<ConflictInjectingSession>(inner_.open_session(), state);
    }

    void bootstrap(const mt::BootstrapSpec& spec) override
    {
        inner_.bootstrap(spec);
    }

    mt::CollectionDescriptor ensure_collection(const mt::CollectionSpec& spec) override
    {
        return inner_.ensure_collection(spec);
    }

    mt::CollectionDescriptor get_collection(std::string_view logicalName) override
    {
        return inner_.get_collection(logicalName);
    }

  private:
    mt::backends::memory::MemoryBackend inner_;
};

struct TestContext
{
    std::shared_ptr<mt::IDatabaseBackend> backend;
    mt::Database database;
    ScriptedWorkflowLogic logic;
    WorkflowOrchestrator orchestrator;

    explicit TestContext(
        WorkflowDefinition definition = makeWorkflowDefinition(),
        std::vector<NextStepDecision> decisions = {},
        std::shared_ptr<mt::IDatabaseBackend> backendArg =
            std::make_shared<mt::backends::memory::MemoryBackend>()
    )
        : backend(std::move(backendArg)),
          database(backend),
          logic(std::move(decisions)),
          orchestrator(
              database,
              logic
          )
    {
        orchestrator.registerWorkflowDefinition(definition);
    }
};

WorkflowExecution startWorkflow(TestContext& context)
{
    return context.orchestrator.startWorkflow("orderProcessing", 1, mt::Json(mt::Json::Object{}));
}

std::vector<WorkflowStepExecution> pollAndClaim(
    TestContext& context,
    const std::string& workerId = "worker-001",
    std::size_t maxResults = 1
)
{
    return context.orchestrator.pollAndClaimWorkflowSteps(
        "orderProcessing", 1, workerId, maxResults
    );
}

WorkflowStepExecution claimStartStep(
    TestContext& context,
    const std::string& workerId = "worker-001"
)
{
    const auto claimed = pollAndClaim(context, workerId);

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].stepName == "validateOrder");

    return claimed[0];
}

void expireLease(
    TestContext& context,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
);

} // namespace

TEST_CASE("orchestrator start creates workflow execution and initial pending step")
{
    TestContext context;

    const auto execution = startWorkflow(context);

    REQUIRE(execution.workflowName == "orderProcessing");
    REQUIRE(execution.workflowVersion == 1);
    REQUIRE(execution.status == WorkflowExecutionStatus::Running);
    REQUIRE(execution.currentStepName == "validateOrder");
    REQUIRE(execution.currentStepAttempt == 0);

    const auto storedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);

    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->currentStepName == "validateOrder");

    const auto stepExecution = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );

    REQUIRE(stepExecution.has_value());
    REQUIRE(stepExecution->workflowExecutionId == execution.workflowExecutionId);
    REQUIRE(stepExecution->workflowName == "orderProcessing");
    REQUIRE(stepExecution->workflowVersion == 1);
    REQUIRE(stepExecution->stepName == "validateOrder");
    REQUIRE(stepExecution->attempt == 0);
    REQUIRE(stepExecution->status == StepExecutionStatus::Pending);
}

TEST_CASE("non-singleton workflow allows multiple running executions")
{
    TestContext context;

    const auto first = startWorkflow(context);
    const auto second = startWorkflow(context);

    REQUIRE(first.workflowExecutionId != second.workflowExecutionId);
    REQUIRE(first.status == WorkflowExecutionStatus::Running);
    REQUIRE(second.status == WorkflowExecutionStatus::Running);
}

TEST_CASE("singleton workflow rejects a second running execution")
{
    auto definition = makeWorkflowDefinition();
    definition.singleton = true;
    TestContext context(definition);

    startWorkflow(context);

    REQUIRE_THROWS_AS(startWorkflow(context), std::runtime_error);
}

TEST_CASE("singleton workflow allows a new execution after completion")
{
    auto definition = makeWorkflowDefinition();
    definition.singleton = true;
    TestContext context(definition, {completeWorkflowDecision()});

    const auto first = startWorkflow(context);
    claimStartStep(context);
    const auto completed = context.orchestrator.completeStep(
        first.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );

    const auto second = startWorkflow(context);

    REQUIRE(completed.status == WorkflowExecutionStatus::Completed);
    REQUIRE(second.workflowExecutionId != first.workflowExecutionId);
    REQUIRE(second.status == WorkflowExecutionStatus::Running);
}

TEST_CASE("singleton workflow remains occupied when lease expiry retries the execution")
{
    auto definition = makeWorkflowDefinition(2);
    definition.singleton = true;
    TestContext context(definition);

    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 1);
    REQUIRE_THROWS_AS(startWorkflow(context), std::runtime_error);
}

TEST_CASE("singleton workflow allows a new execution after lease expiry fails the execution")
{
    auto definition = makeWorkflowDefinition(0);
    definition.singleton = true;
    TestContext context(definition);

    const auto first = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, first.workflowExecutionId, "validateOrder", 0);

    const auto result = context.orchestrator.sweepExpiredLeases();
    const auto second = startWorkflow(context);

    REQUIRE(result.failedCount == 1);
    REQUIRE(second.workflowExecutionId != first.workflowExecutionId);
    REQUIRE(second.status == WorkflowExecutionStatus::Running);
}

TEST_CASE("orchestrator poll-and-claim claims a pending step with a lease")
{
    TestContext context;
    const auto execution = startWorkflow(context);

    const auto claimed = pollAndClaim(context, "worker-001");

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].workflowExecutionId == execution.workflowExecutionId);
    REQUIRE(claimed[0].stepName == "validateOrder");
    REQUIRE(claimed[0].status == StepExecutionStatus::Running);
    REQUIRE(claimed[0].workerId.has_value());
    REQUIRE(claimed[0].workerId.value() == "worker-001");
    REQUIRE(claimed[0].leaseExpiresAt.has_value());
    REQUIRE(claimed[0].leaseExpiresAt.value() > std::chrono::system_clock::now());

    const auto storedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );

    REQUIRE(storedStep.has_value());
    REQUIRE(storedStep->status == StepExecutionStatus::Running);
    REQUIRE(storedStep->workerId.value() == "worker-001");
    REQUIRE(storedStep->leaseExpiresAt.has_value());
}

TEST_CASE("orchestrator keep-alive extends the claimed step lease")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    const auto claimed = claimStartStep(context, "worker-001");

    REQUIRE(claimed.leaseExpiresAt.has_value());
    const auto originalLeaseExpiresAt = claimed.leaseExpiresAt.value();

    const auto keptAlive = context.orchestrator.keepAliveStep(
        execution.workflowExecutionId, "validateOrder", "worker-001"
    );

    REQUIRE(keptAlive.status == StepExecutionStatus::Running);
    REQUIRE(keptAlive.workerId.has_value());
    REQUIRE(keptAlive.workerId.value() == "worker-001");
    REQUIRE(keptAlive.leaseExpiresAt.has_value());
    REQUIRE(keptAlive.leaseExpiresAt.value() > originalLeaseExpiresAt);
}

TEST_CASE("orchestrator complete step with active lease advances to next pending step")
{
    TestContext context(makeWorkflowDefinition(), {nextStepDecision("chargePayment")});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    mt::Json::Object _stepOutputObj;
    _stepOutputObj["valid"] = true;
    mt::Json stepOutput(std::move(_stepOutputObj));

    const auto updatedExecution = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", stepOutput
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Running);
    REQUIRE(updatedExecution.currentStepName == "chargePayment");
    REQUIRE(updatedExecution.currentStepAttempt == 0);
    REQUIRE(context.logic.callCount == 1);
    REQUIRE(context.logic.lastContext.has_value());
    REQUIRE(context.logic.lastContext->completedStepName == "validateOrder");

    const auto completedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );

    REQUIRE(completedStep.has_value());
    REQUIRE(completedStep->status == StepExecutionStatus::Completed);
    REQUIRE(
        (completedStep->output.is_object() && completedStep->output.as_object().count("valid"))
    );
    REQUIRE(completedStep->output.at("valid").as_bool());

    const auto nextStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "chargePayment", 0
    );

    REQUIRE(nextStep.has_value());
    REQUIRE(nextStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("orchestrator rejects complete after lease expiry")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    auto claimedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );

    REQUIRE(claimedStep.has_value());

    claimedStep->leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.orchestrator.putWorkflowStepExecution(*claimedStep);

    REQUIRE_THROWS_AS(
        context.orchestrator.completeStep(
            execution.workflowExecutionId, "validateOrder", "worker-001",
            mt::Json(mt::Json::Object{})
        ),
        std::runtime_error
    );
}

TEST_CASE("orchestrator fail step with active lease creates retry pending step")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    const auto updatedExecution = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "validation service timeout"
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Running);
    REQUIRE(updatedExecution.currentStepName == "validateOrder");
    REQUIRE(updatedExecution.currentStepAttempt == 1);

    const auto failedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );

    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
    REQUIRE(failedStep->failureReason.has_value());
    REQUIRE(failedStep->failureReason.value() == "validation service timeout");

    const auto retryStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 1
    );

    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("orchestrator max retries exceeded marks workflow failed")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    const auto updatedExecution = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "validation failed"
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Failed);
    REQUIRE(updatedExecution.failureReason.has_value());
    REQUIRE(updatedExecution.failureReason.value() == "validation failed");

    const auto storedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);

    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->status == WorkflowExecutionStatus::Failed);

    const auto failedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );

    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
}

TEST_CASE("orchestrator completes workflow when workflow logic returns complete decision")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    const auto updatedExecution = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );

    REQUIRE(updatedExecution.status == WorkflowExecutionStatus::Completed);
    REQUIRE(context.logic.callCount == 1);

    const auto storedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);

    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("orchestrator rejects complete from non-owning worker")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    REQUIRE_THROWS_AS(
        context.orchestrator.completeStep(
            execution.workflowExecutionId, "validateOrder", "worker-002",
            mt::Json(mt::Json::Object{})
        ),
        std::runtime_error
    );
}

TEST_CASE("orchestrator rejects keep-alive from non-owning worker")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    claimStartStep(context, "worker-001");

    REQUIRE_THROWS_AS(
        context.orchestrator.keepAliveStep(
            execution.workflowExecutionId, "validateOrder", "worker-002"
        ),
        std::runtime_error
    );
}

namespace
{

void expireLease(
    TestContext& context,
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    auto step =
        context.orchestrator.getWorkflowStepExecution(workflowExecutionId, stepName, attempt)
            .value();
    step.leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.orchestrator.putWorkflowStepExecution(step);
}

} // namespace

TEST_CASE("sweepExpiredLeases returns {0,0} when no expired steps exist")
{
    TestContext context;
    startWorkflow(context);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 0);
}

TEST_CASE("sweepExpiredLeases retries expired step when attempts remain")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 1);
    REQUIRE(result.failedCount == 0);

    const auto failedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);
    REQUIRE(failedStep->failureReason == "lease expired");
    REQUIRE_FALSE(failedStep->leaseExpiresAt.has_value());

    const auto retryStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 1
    );
    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);

    const auto updatedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);
    REQUIRE(updatedExecution.has_value());
    REQUIRE(updatedExecution->status == WorkflowExecutionStatus::Running);
    REQUIRE(updatedExecution->currentStepAttempt == 1);
}

TEST_CASE("sweepExpiredLeases fails workflow when no retries remain")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 1);

    const auto failedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);

    const auto updatedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);
    REQUIRE(updatedExecution.has_value());
    REQUIRE(updatedExecution->status == WorkflowExecutionStatus::Failed);
    REQUIRE(updatedExecution->failureReason == "lease expired");
}

TEST_CASE("sweepExpiredLeases is idempotent on a second call after retry")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    context.orchestrator.sweepExpiredLeases();

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 0);
}

TEST_CASE("sweepExpiredLeases skips step whose workflow execution is not running")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    auto runningStep =
        context.orchestrator
            .getWorkflowStepExecution(execution.workflowExecutionId, "validateOrder", 0)
            .value();
    runningStep.leaseExpiresAt = std::chrono::system_clock::now() - std::chrono::seconds{1};
    context.orchestrator.putWorkflowStepExecution(runningStep);

    auto canceledExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId).value();
    canceledExecution.status = WorkflowExecutionStatus::Canceled;
    context.orchestrator.putWorkflowExecution(canceledExecution);

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(result.retriedCount == 0);
    REQUIRE(result.failedCount == 0);
}

TEST_CASE("startWorkflow creates initial step with createdAt set")
{
    TestContext context;
    const auto before = std::chrono::system_clock::now();
    const auto execution = startWorkflow(context);
    const auto after = std::chrono::system_clock::now();

    const auto step = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(step.has_value());
    REQUIRE(step->createdAt.has_value());
    REQUIRE(step->createdAt.value() >= before);
    REQUIRE(step->createdAt.value() <= after);
}

TEST_CASE("pollAndClaim sets startedAt on claimed step")
{
    TestContext context;
    const auto execution = startWorkflow(context);

    const auto before = std::chrono::system_clock::now();
    claimStartStep(context);
    const auto after = std::chrono::system_clock::now();

    const auto step = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(step.has_value());
    REQUIRE(step->startedAt.has_value());
    REQUIRE(step->startedAt.value() >= before);
    REQUIRE(step->startedAt.value() <= after);
}

TEST_CASE("completeStep sets completedAt on step execution")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );
    const auto after = std::chrono::system_clock::now();

    const auto step = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(step.has_value());
    REQUIRE(step->completedAt.has_value());
    REQUIRE(step->completedAt.value() >= before);
    REQUIRE(step->completedAt.value() <= after);
}

TEST_CASE("failStep sets completedAt on step execution")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );
    const auto after = std::chrono::system_clock::now();

    const auto step = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(step.has_value());
    REQUIRE(step->completedAt.has_value());
    REQUIRE(step->completedAt.value() >= before);
    REQUIRE(step->completedAt.value() <= after);
}

TEST_CASE("sweepExpiredLeases sets completedAt on the failed step")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.sweepExpiredLeases();
    const auto after = std::chrono::system_clock::now();

    const auto step = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(step.has_value());
    REQUIRE(step->completedAt.has_value());
    REQUIRE(step->completedAt.value() >= before);
    REQUIRE(step->completedAt.value() <= after);
}

TEST_CASE("startWorkflow sets startedAt on workflow execution")
{
    TestContext context;

    const auto before = std::chrono::system_clock::now();
    const auto execution = startWorkflow(context);
    const auto after = std::chrono::system_clock::now();

    REQUIRE(execution.startedAt.has_value());
    REQUIRE(execution.startedAt.value() >= before);
    REQUIRE(execution.startedAt.value() <= after);

    const auto stored = context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);
    REQUIRE(stored->startedAt.has_value());
    REQUIRE(stored->startedAt.value() == execution.startedAt.value());
}

TEST_CASE("completeStep sets completedAt on workflow execution when workflow completes")
{
    TestContext context(makeWorkflowDefinition(), {completeWorkflowDecision()});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );
    const auto after = std::chrono::system_clock::now();

    REQUIRE(result.completedAt.has_value());
    REQUIRE(result.completedAt.value() >= before);
    REQUIRE(result.completedAt.value() <= after);
}

TEST_CASE("completeStep does not set completedAt when workflow continues to next step")
{
    TestContext context(makeWorkflowDefinition(), {nextStepDecision("chargePayment")});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );

    REQUIRE_FALSE(result.completedAt.has_value());
}

TEST_CASE("completeStep with next step delay keeps the next pending step unclaimable")
{
    TestContext context(makeWorkflowDefinition(), {nextStepDecision("chargePayment")});
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{}),
        std::chrono::seconds{60}
    );

    REQUIRE(result.status == WorkflowExecutionStatus::Running);

    const auto nextStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "chargePayment", 0
    );
    REQUIRE(nextStep.has_value());
    REQUIRE(nextStep->status == StepExecutionStatus::Pending);
    REQUIRE(nextStep->scheduledAt.has_value());
    REQUIRE(nextStep->scheduledAt.value() >= before + std::chrono::seconds{60});

    const auto claimed = pollAndClaim(context, "worker-002");
    REQUIRE(claimed.empty());
}

TEST_CASE("completeStep retries and succeeds after an mt execution row conflict")
{
    TestContext context(
        makeWorkflowDefinition(), {completeWorkflowDecision(), completeWorkflowDecision()}
    );
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    context.logic.onDecide = [&](const StepCompletionContext& completionContext, int callCount)
    {
        if (callCount != 1)
        {
            return;
        }

        auto conflictingExecution =
            context.orchestrator.getWorkflowExecution(completionContext.workflowExecutionId)
                .value();
        mt::Json::Object state;
        state["conflictingWrite"] = true;
        conflictingExecution.state = mt::Json(std::move(state));
        context.orchestrator.putWorkflowExecution(conflictingExecution);
    };

    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );

    REQUIRE(context.logic.callCount == 2);
    REQUIRE(result.status == WorkflowExecutionStatus::Completed);

    const auto storedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);
    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->status == WorkflowExecutionStatus::Completed);

    const auto completedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(completedStep.has_value());
    REQUIRE(completedStep->status == StepExecutionStatus::Completed);
}

TEST_CASE("completeStep retries and succeeds after an mt step row conflict")
{
    TestContext context(
        makeWorkflowDefinition(), {completeWorkflowDecision(), completeWorkflowDecision()}
    );
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    context.logic.onDecide = [&](const StepCompletionContext& completionContext, int callCount)
    {
        if (callCount != 1)
        {
            return;
        }

        auto conflictingStep =
            context.orchestrator
                .getWorkflowStepExecution(
                    completionContext.workflowExecutionId, completionContext.completedStepName, 0
                )
                .value();
        context.orchestrator.putWorkflowStepExecution(conflictingStep);
    };

    const auto result = context.orchestrator.completeStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", mt::Json(mt::Json::Object{})
    );

    REQUIRE(context.logic.callCount == 2);
    REQUIRE(result.status == WorkflowExecutionStatus::Completed);

    const auto completedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(completedStep.has_value());
    REQUIRE(completedStep->status == StepExecutionStatus::Completed);
    REQUIRE_FALSE(completedStep->leaseExpiresAt.has_value());
}

TEST_CASE("startWorkflow retries and succeeds after an mt definition row conflict")
{
    auto backend = std::make_shared<ConflictInjectingBackend>();
    TestContext context(makeWorkflowDefinition(), {}, backend);

    backend->state->onReadSnapshot = [&]()
    {
        auto definition = context.orchestrator.getWorkflowDefinition("orderProcessing", 1).value();
        definition.expectedExecutionTime = "PT11M";
        context.orchestrator.registerWorkflowDefinition(definition);
    };

    const auto execution = startWorkflow(context);

    REQUIRE(backend->state->readSnapshotInjections == 1);
    REQUIRE(execution.status == WorkflowExecutionStatus::Running);
    REQUIRE(execution.currentStepName == "validateOrder");

    const auto initialStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(initialStep.has_value());
    REQUIRE(initialStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("pollAndClaimWorkflowSteps retries and succeeds after an mt predicate conflict")
{
    auto backend = std::make_shared<ConflictInjectingBackend>();
    TestContext context(makeWorkflowDefinition(), {}, backend);
    startWorkflow(context);

    backend->state->onQuerySnapshot = [&]() { startWorkflow(context); };

    const auto claimed = pollAndClaim(context, "worker-001", 1);

    REQUIRE(backend->state->querySnapshotInjections == 1);
    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].status == StepExecutionStatus::Running);
    REQUIRE(claimed[0].workerId == "worker-001");
}

TEST_CASE("keepAliveStep retries and succeeds after an mt execution row conflict")
{
    auto backend = std::make_shared<ConflictInjectingBackend>();
    TestContext context(makeWorkflowDefinition(), {}, backend);
    const auto execution = startWorkflow(context);
    const auto claimed = claimStartStep(context);

    REQUIRE(claimed.leaseExpiresAt.has_value());
    const auto originalLeaseExpiresAt = claimed.leaseExpiresAt.value();

    backend->state->onReadSnapshot = [&]()
    {
        auto conflictingExecution =
            context.orchestrator.getWorkflowExecution(execution.workflowExecutionId).value();
        mt::Json::Object state;
        state["conflictingWrite"] = true;
        conflictingExecution.state = mt::Json(std::move(state));
        context.orchestrator.putWorkflowExecution(conflictingExecution);
    };

    const auto keptAlive = context.orchestrator.keepAliveStep(
        execution.workflowExecutionId, "validateOrder", "worker-001"
    );

    REQUIRE(backend->state->readSnapshotInjections == 1);
    REQUIRE(keptAlive.status == StepExecutionStatus::Running);
    REQUIRE(keptAlive.workerId == "worker-001");
    REQUIRE(keptAlive.leaseExpiresAt.has_value());
    REQUIRE(keptAlive.leaseExpiresAt.value() > originalLeaseExpiresAt);
}

TEST_CASE("failStep retries and succeeds after an mt execution row conflict")
{
    auto backend = std::make_shared<ConflictInjectingBackend>();
    TestContext context(makeWorkflowDefinition(2), {}, backend);
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    backend->state->onReadSnapshot = [&]()
    {
        auto conflictingExecution =
            context.orchestrator.getWorkflowExecution(execution.workflowExecutionId).value();
        mt::Json::Object state;
        state["conflictingWrite"] = true;
        conflictingExecution.state = mt::Json(std::move(state));
        context.orchestrator.putWorkflowExecution(conflictingExecution);
    };

    const auto result = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );

    REQUIRE(backend->state->readSnapshotInjections == 1);
    REQUIRE(result.status == WorkflowExecutionStatus::Running);
    REQUIRE(result.currentStepAttempt == 1);

    const auto failedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);

    const auto retryStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 1
    );
    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("sweepExpiredLeases retries and succeeds after an mt predicate conflict")
{
    auto backend = std::make_shared<ConflictInjectingBackend>();
    TestContext context(makeWorkflowDefinition(2), {}, backend);
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    backend->state->onQuerySnapshot = [&]()
    {
        auto conflictingStep =
            context.orchestrator
                .getWorkflowStepExecution(execution.workflowExecutionId, "validateOrder", 0)
                .value();
        mt::Json::Object state;
        state["conflictingWrite"] = true;
        conflictingStep.state = mt::Json(std::move(state));
        context.orchestrator.putWorkflowStepExecution(conflictingStep);
    };

    const auto result = context.orchestrator.sweepExpiredLeases();

    REQUIRE(backend->state->querySnapshotInjections == 1);
    REQUIRE(result.retriedCount == 1);
    REQUIRE(result.failedCount == 0);

    const auto retryStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 1
    );
    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("failStep sets completedAt on workflow execution when retries are exhausted")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );
    const auto after = std::chrono::system_clock::now();

    REQUIRE(result.status == WorkflowExecutionStatus::Failed);
    REQUIRE(result.completedAt.has_value());
    REQUIRE(result.completedAt.value() >= before);
    REQUIRE(result.completedAt.value() <= after);
}

TEST_CASE("failStep does not set completedAt when retries remain")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);

    const auto result = context.orchestrator.failStep(
        execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );

    REQUIRE(result.status == WorkflowExecutionStatus::Running);
    REQUIRE_FALSE(result.completedAt.has_value());
}

TEST_CASE("cancelWorkflow sets completedAt on workflow execution")
{
    TestContext context;
    const auto execution = startWorkflow(context);

    const auto before = std::chrono::system_clock::now();
    const auto result = context.orchestrator.cancelWorkflow(execution.workflowExecutionId);
    const auto after = std::chrono::system_clock::now();

    REQUIRE(result.completedAt.has_value());
    REQUIRE(result.completedAt.value() >= before);
    REQUIRE(result.completedAt.value() <= after);
}

TEST_CASE("sweepExpiredLeases sets completedAt on workflow execution when failed")
{
    TestContext context(makeWorkflowDefinition(0));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    expireLease(context, execution.workflowExecutionId, "validateOrder", 0);

    const auto before = std::chrono::system_clock::now();
    context.orchestrator.sweepExpiredLeases();
    const auto after = std::chrono::system_clock::now();

    const auto stored = context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);
    REQUIRE(stored->completedAt.has_value());
    REQUIRE(stored->completedAt.value() >= before);
    REQUIRE(stored->completedAt.value() <= after);
}

TEST_CASE("independent workflow executions can be completed concurrently without deadlock")
{
    TestContext context(
        makeWorkflowDefinition(), {completeWorkflowDecision(), completeWorkflowDecision()}
    );

    const auto execA = startWorkflow(context);
    const auto execB = startWorkflow(context);

    const auto stepsA = pollAndClaim(context, "worker-A");
    const auto stepsB = pollAndClaim(context, "worker-B");

    REQUIRE(stepsA.size() == 1);
    REQUIRE(stepsB.size() == 1);

    WorkflowExecution resultA;
    WorkflowExecution resultB;

    std::thread threadA(
        [&]()
        {
            resultA = context.orchestrator.completeStep(
                execA.workflowExecutionId, "validateOrder", "worker-A", mt::Json(mt::Json::Object{})
            );
        }
    );

    std::thread threadB(
        [&]()
        {
            resultB = context.orchestrator.completeStep(
                execB.workflowExecutionId, "validateOrder", "worker-B", mt::Json(mt::Json::Object{})
            );
        }
    );

    threadA.join();
    threadB.join();

    REQUIRE(resultA.status == WorkflowExecutionStatus::Completed);
    REQUIRE(resultB.status == WorkflowExecutionStatus::Completed);
}

TEST_CASE("transaction-aware registerWorkflowDefinition commits with caller transaction")
{
    TestContext context(makeWorkflowDefinition());
    mt::TransactionProvider transactions{context.database};

    auto definition = makeWorkflowDefinition();
    definition.workflowVersion = 2;

    auto tx = transactions.begin();
    context.orchestrator.registerWorkflowDefinition(tx, definition);
    tx.commit();

    const auto stored = context.orchestrator.getWorkflowDefinition("orderProcessing", 2);
    REQUIRE(stored.has_value());
    REQUIRE(stored->workflowVersion == 2);
}

TEST_CASE("transaction-aware startWorkflow rolls back when caller aborts transaction")
{
    TestContext context;
    mt::TransactionProvider transactions{context.database};

    auto tx = transactions.begin();
    const auto execution =
        context.orchestrator.startWorkflow(tx, "orderProcessing", 1, mt::Json(mt::Json::Object{}));
    tx.abort();

    REQUIRE_FALSE(context.orchestrator.getWorkflowExecution(execution.workflowExecutionId));
    REQUIRE_FALSE(context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    ));
}

TEST_CASE("transaction-aware startWorkflow commits execution and initial step")
{
    TestContext context;
    mt::TransactionProvider transactions{context.database};

    auto tx = transactions.begin();
    const auto execution =
        context.orchestrator.startWorkflow(tx, "orderProcessing", 1, mt::Json(mt::Json::Object{}));
    tx.commit();

    const auto storedExecution =
        context.orchestrator.getWorkflowExecution(execution.workflowExecutionId);
    REQUIRE(storedExecution.has_value());
    REQUIRE(storedExecution->status == WorkflowExecutionStatus::Running);

    const auto storedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(storedStep.has_value());
    REQUIRE(storedStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("transaction-aware pollAndClaimWorkflowSteps persists claim on caller commit")
{
    TestContext context;
    const auto execution = startWorkflow(context);
    mt::TransactionProvider transactions{context.database};

    auto tx = transactions.begin();
    const auto claimed =
        context.orchestrator.pollAndClaimWorkflowSteps(tx, "orderProcessing", 1, "worker-tx", 1);
    tx.commit();

    REQUIRE(claimed.size() == 1);
    REQUIRE(claimed[0].workflowExecutionId == execution.workflowExecutionId);

    const auto storedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(storedStep.has_value());
    REQUIRE(storedStep->status == StepExecutionStatus::Running);
    REQUIRE(storedStep->workerId == "worker-tx");
}

TEST_CASE("transaction-aware failStep persists retry step on caller commit")
{
    TestContext context(makeWorkflowDefinition(2));
    const auto execution = startWorkflow(context);
    claimStartStep(context);
    mt::TransactionProvider transactions{context.database};

    auto tx = transactions.begin();
    const auto result = context.orchestrator.failStep(
        tx, execution.workflowExecutionId, "validateOrder", "worker-001", "timeout"
    );
    tx.commit();

    REQUIRE(result.currentStepAttempt == 1);

    const auto failedStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 0
    );
    REQUIRE(failedStep.has_value());
    REQUIRE(failedStep->status == StepExecutionStatus::Failed);

    const auto retryStep = context.orchestrator.getWorkflowStepExecution(
        execution.workflowExecutionId, "validateOrder", 1
    );
    REQUIRE(retryStep.has_value());
    REQUIRE(retryStep->status == StepExecutionStatus::Pending);
}

TEST_CASE("transaction-aware methods do not retry caller transaction conflicts")
{
    TestContext context;
    mt::TransactionProvider transactions{context.database};

    auto tx = transactions.begin();
    const auto execution =
        context.orchestrator.startWorkflow(tx, "orderProcessing", 1, mt::Json(mt::Json::Object{}));

    auto conflictingDefinition =
        context.orchestrator.getWorkflowDefinition("orderProcessing", 1).value();
    conflictingDefinition.expectedExecutionTime = "PT11M";
    context.orchestrator.registerWorkflowDefinition(conflictingDefinition);

    REQUIRE_THROWS_AS(tx.commit(), mt::TransactionConflict);
    REQUIRE_FALSE(context.orchestrator.getWorkflowExecution(execution.workflowExecutionId));
}

TEST_CASE("transaction-aware singleton starts conflict on the singleton lock row")
{
    auto definition = makeWorkflowDefinition();
    definition.singleton = true;
    TestContext context(definition);
    mt::TransactionProvider transactions{context.database};

    auto tx1 = transactions.begin();
    auto tx2 = transactions.begin();

    const auto first =
        context.orchestrator.startWorkflow(tx1, "orderProcessing", 1, mt::Json(mt::Json::Object{}));
    context.orchestrator.startWorkflow(tx2, "orderProcessing", 1, mt::Json(mt::Json::Object{}));

    tx1.commit();

    REQUIRE_THROWS_AS(tx2.commit(), mt::TransactionConflict);

    const auto stored = context.orchestrator.getWorkflowExecution(first.workflowExecutionId);
    REQUIRE(stored.has_value());
    REQUIRE(stored->status == WorkflowExecutionStatus::Running);
}
