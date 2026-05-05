#include "wf/backend/sqlite/sqlite_workflow_step_execution_store.hpp"

#include "sqlite_helpers.hpp"
#include "wf/json.hpp"

#include <stdexcept>

namespace workflow::backend::sqlite
{

namespace
{

void validateIdentity(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }

    if (stepName.empty())
    {
        throw std::invalid_argument("stepName must not be empty");
    }

    if (attempt < 0)
    {
        throw std::invalid_argument("attempt must be greater than or equal to 0");
    }
}

void validateStepExecution(const WorkflowStepExecution& s)
{
    validateIdentity(s.workflowExecutionId, s.stepName, s.attempt);

    if (s.workflowName.empty())
    {
        throw std::invalid_argument("workflowName must not be empty");
    }

    if (s.workflowVersion < 1)
    {
        throw std::invalid_argument("workflowVersion must be greater than or equal to 1");
    }
}

void validateWorkerId(const std::string& workerId)
{
    if (workerId.empty())
    {
        throw std::invalid_argument("workerId must not be empty");
    }
}

void validateLeaseDuration(std::chrono::seconds leaseDuration)
{
    if (leaseDuration <= std::chrono::seconds{0})
    {
        throw std::invalid_argument("leaseDuration must be greater than 0 seconds");
    }
}

void validatePollAndClaimRequest(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults,
    const std::map<
        std::string,
        std::chrono::seconds>& leaseDurationsByStepName
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

    validateWorkerId(workerId);

    if (maxResults == 0)
    {
        throw std::invalid_argument("maxResults must be greater than 0");
    }

    for (const auto& [stepName, leaseDuration] : leaseDurationsByStepName)
    {
        if (stepName.empty())
        {
            throw std::invalid_argument("lease duration step name must not be empty");
        }

        validateLeaseDuration(leaseDuration);
    }
}

WorkflowStepExecution rowToStepExecution(sqlite3_stmt* stmt)
{
    WorkflowStepExecution s;
    s.workflowExecutionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    s.workflowName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    s.workflowVersion = sqlite3_column_int(stmt, 2);
    s.stepName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    s.attempt = sqlite3_column_int(stmt, 4);
    s.status = toStepStatus(sqlite3_column_int(stmt, 5));

    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
    {
        s.workerId = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
    }

    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
    {
        s.leaseExpiresAt = fromMs(sqlite3_column_int64(stmt, 7));
    }

    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
    {
        s.failureReason = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    }

    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
    {
        s.createdAt = fromMs(sqlite3_column_int64(stmt, 9));
    }

    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
    {
        s.startedAt = fromMs(sqlite3_column_int64(stmt, 10));
    }

    if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
    {
        s.completedAt = fromMs(sqlite3_column_int64(stmt, 11));
    }

    s.input = json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12)));
    s.state = json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13)));
    s.output = json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)));

    return s;
}

constexpr const char* SELECT_STEP_COLS =
    "workflow_execution_id, workflow_name, workflow_version, step_name, attempt, "
    "status, worker_id, lease_expires_at_ms, failure_reason, "
    "created_at_ms, started_at_ms, completed_at_ms, "
    "input_json, state_json, output_json";

static const std::string SQL_SAVE_STEP = "INSERT OR REPLACE INTO workflow_step_executions (" +
                                         std::string(SELECT_STEP_COLS) +
                                         ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

static const std::string SQL_SELECT_STEP_BY_ID =
    "SELECT " + std::string(SELECT_STEP_COLS) +
    " FROM workflow_step_executions "
    "WHERE workflow_execution_id = ? AND step_name = ? AND attempt = ?";

static const std::string SQL_POLL_FIND =
    "SELECT " + std::string(SELECT_STEP_COLS) +
    " FROM workflow_step_executions "
    "WHERE workflow_name = ? AND workflow_version = ? "
    "  AND (status = 0 OR (status = 1 AND lease_expires_at_ms <= ?))";

static const std::string SQL_EXPIRED_RUNNING =
    "SELECT " + std::string(SELECT_STEP_COLS) +
    " FROM workflow_step_executions "
    "WHERE status = 1 AND lease_expires_at_ms IS NOT NULL AND lease_expires_at_ms <= ?";

constexpr const char* SQL_POLL_UPDATE =
    "UPDATE workflow_step_executions "
    "SET status = 1, worker_id = ?, lease_expires_at_ms = ?, "
    "    failure_reason = NULL, started_at_ms = ? "
    "WHERE workflow_execution_id = ? AND step_name = ? AND attempt = ?";

constexpr const char* SQL_KEEP_ALIVE_UPDATE =
    "UPDATE workflow_step_executions SET lease_expires_at_ms = ? "
    "WHERE workflow_execution_id = ? AND step_name = ? AND attempt = ?";

constexpr const char* SQL_UPDATE_STEP =
    "UPDATE workflow_step_executions SET "
    "  workflow_name = ?, workflow_version = ?, status = ?, "
    "  worker_id = ?, lease_expires_at_ms = ?, failure_reason = ?, "
    "  created_at_ms = ?, started_at_ms = ?, completed_at_ms = ?, "
    "  input_json = ?, state_json = ?, output_json = ? "
    "WHERE workflow_execution_id = ? AND step_name = ? AND attempt = ?";

constexpr const char* SQL_CANCEL_BY_EXECUTION =
    "UPDATE workflow_step_executions "
    "SET status = 4, worker_id = NULL, lease_expires_at_ms = NULL, completed_at_ms = ? "
    "WHERE workflow_execution_id = ? AND status IN (0, 1)";

constexpr const char* SQL_REMOVE_STEP =
    "DELETE FROM workflow_step_executions "
    "WHERE workflow_execution_id = ? AND step_name = ? AND attempt = ?";

void bindStepExecution(
    sqlite3_stmt* stmt,
    const WorkflowStepExecution& s,
    int startCol = 1
)
{
    int col = startCol;

    sqlite3_bind_text(stmt, col++, s.workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, s.workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, col++, s.workflowVersion);
    sqlite3_bind_text(stmt, col++, s.stepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, col++, s.attempt);
    sqlite3_bind_int(stmt, col++, toInt(s.status));

    if (s.workerId.has_value())
    {
        sqlite3_bind_text(stmt, col++, s.workerId.value().c_str(), -1, SQLITE_TRANSIENT);
    }
    else
    {
        sqlite3_bind_null(stmt, col++);
    }

    if (s.leaseExpiresAt.has_value())
    {
        sqlite3_bind_int64(stmt, col++, toMs(s.leaseExpiresAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt, col++);
    }

    if (s.failureReason.has_value())
    {
        sqlite3_bind_text(stmt, col++, s.failureReason.value().c_str(), -1, SQLITE_TRANSIENT);
    }
    else
    {
        sqlite3_bind_null(stmt, col++);
    }

    if (s.createdAt.has_value())
    {
        sqlite3_bind_int64(stmt, col++, toMs(s.createdAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt, col++);
    }

    if (s.startedAt.has_value())
    {
        sqlite3_bind_int64(stmt, col++, toMs(s.startedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt, col++);
    }

    if (s.completedAt.has_value())
    {
        sqlite3_bind_int64(stmt, col++, toMs(s.completedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt, col++);
    }

    const auto inputJson = json::stringify(s.input);
    const auto stateJson = json::stringify(s.state);
    const auto outputJson = json::stringify(s.output);

    sqlite3_bind_text(stmt, col++, inputJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, stateJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, outputJson.c_str(), -1, SQLITE_TRANSIENT);
}

} // namespace

SQLiteWorkflowStepExecutionStore::SQLiteWorkflowStepExecutionStore(SQLiteDatabase& db)
    : db_(db)
{
}

void SQLiteWorkflowStepExecutionStore::save(const WorkflowStepExecution& stepExecution)
{
    validateStepExecution(stepExecution);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(db_.handle(), SQL_SAVE_STEP);

    bindStepExecution(stmt.get(), stepExecution);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            std::string("sqlite save step execution failed: ") + sqlite3_errmsg(db_.handle())
        );
    }
}

std::optional<WorkflowStepExecution> SQLiteWorkflowStepExecutionStore::find(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
) const
{
    validateIdentity(workflowExecutionId, stepName, attempt);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(db_.handle(), SQL_SELECT_STEP_BY_ID);

    sqlite3_bind_text(stmt.get(), 1, workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, stepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, attempt);

    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    return rowToStepExecution(stmt.get());
}

std::vector<WorkflowStepExecution> SQLiteWorkflowStepExecutionStore::pollAndClaim(
    const std::string& workflowName,
    int workflowVersion,
    const std::string& workerId,
    std::size_t maxResults,
    const std::map<
        std::string,
        std::chrono::seconds>& leaseDurationsByStepName
)
{
    validatePollAndClaimRequest(
        workflowName, workflowVersion, workerId, maxResults, leaseDurationsByStepName
    );

    const auto now = std::chrono::system_clock::now();
    const auto nowMs = toMs(now);

    std::lock_guard<std::mutex> lock(db_.mutex());

    exec(db_.handle(), "BEGIN");

    try
    {
        Stmt findStmt(db_.handle(), SQL_POLL_FIND);

        sqlite3_bind_text(findStmt.get(), 1, workflowName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(findStmt.get(), 2, workflowVersion);
        sqlite3_bind_int64(findStmt.get(), 3, nowMs);

        std::vector<WorkflowStepExecution> candidates;

        while (sqlite3_step(findStmt.get()) == SQLITE_ROW)
        {
            candidates.push_back(rowToStepExecution(findStmt.get()));
        }

        std::vector<WorkflowStepExecution> result;

        Stmt updateStmt(db_.handle(), SQL_POLL_UPDATE);

        for (auto& step : candidates)
        {
            if (result.size() >= maxResults)
            {
                break;
            }

            const auto durationIter = leaseDurationsByStepName.find(step.stepName);

            if (durationIter == leaseDurationsByStepName.end())
            {
                continue;
            }

            const auto leaseMs = toMs(now + durationIter->second);

            sqlite3_reset(updateStmt.get());
            sqlite3_bind_text(updateStmt.get(), 1, workerId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(updateStmt.get(), 2, leaseMs);
            sqlite3_bind_int64(updateStmt.get(), 3, nowMs);
            sqlite3_bind_text(
                updateStmt.get(), 4, step.workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT
            );
            sqlite3_bind_text(updateStmt.get(), 5, step.stepName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(updateStmt.get(), 6, step.attempt);

            if (sqlite3_step(updateStmt.get()) != SQLITE_DONE)
            {
                throw std::runtime_error(
                    std::string("sqlite pollAndClaim update failed: ") +
                    sqlite3_errmsg(db_.handle())
                );
            }

            step.status = StepExecutionStatus::Running;
            step.workerId = workerId;
            step.leaseExpiresAt = now + durationIter->second;
            step.failureReason.reset();
            step.startedAt = now;

            result.push_back(std::move(step));
        }

        exec(db_.handle(), "COMMIT");
        return result;
    }
    catch (...)
    {
        exec(db_.handle(), "ROLLBACK");
        throw;
    }
}

WorkflowStepExecution SQLiteWorkflowStepExecutionStore::keepAlive(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt,
    const std::string& workerId,
    std::chrono::seconds leaseDuration
)
{
    validateIdentity(workflowExecutionId, stepName, attempt);
    validateWorkerId(workerId);
    validateLeaseDuration(leaseDuration);

    const auto now = std::chrono::system_clock::now();
    const auto newLeaseExpiresAtMs = toMs(now + leaseDuration);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt findStmt(db_.handle(), SQL_SELECT_STEP_BY_ID);

    sqlite3_bind_text(findStmt.get(), 1, workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(findStmt.get(), 2, stepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(findStmt.get(), 3, attempt);

    if (sqlite3_step(findStmt.get()) != SQLITE_ROW)
    {
        throw std::runtime_error(
            "workflow step execution not found: " + workflowExecutionId + "/" + stepName
        );
    }

    auto step = rowToStepExecution(findStmt.get());

    if (step.status != StepExecutionStatus::Running)
    {
        throw std::runtime_error(
            "workflow step execution is not running: " + workflowExecutionId + "/" + stepName
        );
    }

    if (!step.workerId.has_value() || step.workerId.value() != workerId)
    {
        throw std::runtime_error(
            "workflow step execution is owned by a different worker: " + workflowExecutionId + "/" +
            stepName
        );
    }

    if (!step.leaseExpiresAt.has_value())
    {
        throw std::runtime_error(
            "workflow step execution does not have an active lease: " + workflowExecutionId + "/" +
            stepName
        );
    }

    if (step.leaseExpiresAt.value() <= now)
    {
        throw std::runtime_error(
            "workflow step execution lease has expired: " + workflowExecutionId + "/" + stepName
        );
    }

    Stmt updateStmt(db_.handle(), SQL_KEEP_ALIVE_UPDATE);

    sqlite3_bind_int64(updateStmt.get(), 1, newLeaseExpiresAtMs);
    sqlite3_bind_text(updateStmt.get(), 2, workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(updateStmt.get(), 3, stepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(updateStmt.get(), 4, attempt);

    if (sqlite3_step(updateStmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            std::string("sqlite keepAlive update failed: ") + sqlite3_errmsg(db_.handle())
        );
    }

    step.leaseExpiresAt = now + leaseDuration;
    return step;
}

void SQLiteWorkflowStepExecutionStore::update(const WorkflowStepExecution& stepExecution)
{
    validateStepExecution(stepExecution);

    std::lock_guard<std::mutex> lock(db_.mutex());

    const auto inputJson = json::stringify(stepExecution.input);
    const auto stateJson = json::stringify(stepExecution.state);
    const auto outputJson = json::stringify(stepExecution.output);

    Stmt stmt(db_.handle(), SQL_UPDATE_STEP);

    sqlite3_bind_text(stmt.get(), 1, stepExecution.workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, stepExecution.workflowVersion);
    sqlite3_bind_int(stmt.get(), 3, toInt(stepExecution.status));

    if (stepExecution.workerId.has_value())
    {
        sqlite3_bind_text(
            stmt.get(), 4, stepExecution.workerId.value().c_str(), -1, SQLITE_TRANSIENT
        );
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 4);
    }

    if (stepExecution.leaseExpiresAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 5, toMs(stepExecution.leaseExpiresAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 5);
    }

    if (stepExecution.failureReason.has_value())
    {
        sqlite3_bind_text(
            stmt.get(), 6, stepExecution.failureReason.value().c_str(), -1, SQLITE_TRANSIENT
        );
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 6);
    }

    if (stepExecution.createdAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 7, toMs(stepExecution.createdAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 7);
    }

    if (stepExecution.startedAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 8, toMs(stepExecution.startedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 8);
    }

    if (stepExecution.completedAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 9, toMs(stepExecution.completedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 9);
    }

    sqlite3_bind_text(stmt.get(), 10, inputJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 11, stateJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 12, outputJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(
        stmt.get(), 13, stepExecution.workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT
    );
    sqlite3_bind_text(stmt.get(), 14, stepExecution.stepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 15, stepExecution.attempt);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            std::string("sqlite update step execution failed: ") + sqlite3_errmsg(db_.handle())
        );
    }

    if (sqlite3_changes(db_.handle()) == 0)
    {
        throw std::runtime_error(
            "cannot update missing workflow step execution: " + stepExecution.workflowExecutionId +
            "/" + stepExecution.stepName
        );
    }
}

void SQLiteWorkflowStepExecutionStore::cancelByExecution(const std::string& workflowExecutionId)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }

    const auto nowMs = toMs(std::chrono::system_clock::now());

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(db_.handle(), SQL_CANCEL_BY_EXECUTION);

    sqlite3_bind_int64(stmt.get(), 1, nowMs);
    sqlite3_bind_text(stmt.get(), 2, workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt.get());
}

std::vector<WorkflowStepExecution> SQLiteWorkflowStepExecutionStore::findExpiredRunning() const
{
    const auto nowMs = toMs(std::chrono::system_clock::now());

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(db_.handle(), SQL_EXPIRED_RUNNING);

    sqlite3_bind_int64(stmt.get(), 1, nowMs);

    std::vector<WorkflowStepExecution> result;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
    {
        result.push_back(rowToStepExecution(stmt.get()));
    }

    return result;
}

void SQLiteWorkflowStepExecutionStore::remove(
    const std::string& workflowExecutionId,
    const std::string& stepName,
    int attempt
)
{
    validateIdentity(workflowExecutionId, stepName, attempt);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(db_.handle(), SQL_REMOVE_STEP);

    sqlite3_bind_text(stmt.get(), 1, workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, stepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, attempt);

    sqlite3_step(stmt.get());
}

} // namespace workflow::backend::sqlite
