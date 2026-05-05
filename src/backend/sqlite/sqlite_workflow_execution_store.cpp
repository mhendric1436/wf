#include "wf/backend/sqlite/sqlite_workflow_execution_store.hpp"

#include "sqlite_helpers.hpp"
#include "wf/json.hpp"

#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace workflow::backend::sqlite
{

namespace
{

void validateExecutionId(const std::string& workflowExecutionId)
{
    if (workflowExecutionId.empty())
    {
        throw std::invalid_argument("workflowExecutionId must not be empty");
    }
}

WorkflowExecution rowToExecution(sqlite3_stmt* stmt)
{
    WorkflowExecution exec;
    exec.workflowExecutionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    exec.workflowName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    exec.workflowVersion = sqlite3_column_int(stmt, 2);
    exec.status = toExecutionStatus(sqlite3_column_int(stmt, 3));
    exec.currentStepName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    exec.input = json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
    exec.state = json::parse(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
    exec.currentStepAttempt = sqlite3_column_int(stmt, 7);

    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
    {
        exec.failureReason =
            std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    }

    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
    {
        exec.startedAt = fromMs(sqlite3_column_int64(stmt, 9));
    }

    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
    {
        exec.completedAt = fromMs(sqlite3_column_int64(stmt, 10));
    }

    return exec;
}

} // namespace

SQLiteWorkflowExecutionStore::SQLiteWorkflowExecutionStore(SQLiteDatabase& db)
    : db_(db)
{
}

std::string SQLiteWorkflowExecutionStore::generateExecutionId()
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

void SQLiteWorkflowExecutionStore::save(const WorkflowExecution& execution)
{
    validateExecutionId(execution.workflowExecutionId);

    const auto inputJson = json::stringify(execution.input);
    const auto stateJson = json::stringify(execution.state);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(), "INSERT OR REPLACE INTO workflow_executions "
                      "(workflow_execution_id, workflow_name, workflow_version, status, "
                      " current_step_name, input_json, state_json, current_step_attempt, "
                      " failure_reason, started_at_ms, completed_at_ms) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    sqlite3_bind_text(stmt.get(), 1, execution.workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, execution.workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, execution.workflowVersion);
    sqlite3_bind_int(stmt.get(), 4, toInt(execution.status));
    sqlite3_bind_text(stmt.get(), 5, execution.currentStepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, inputJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 7, stateJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 8, execution.currentStepAttempt);

    if (execution.failureReason.has_value())
    {
        sqlite3_bind_text(
            stmt.get(), 9, execution.failureReason.value().c_str(), -1, SQLITE_TRANSIENT
        );
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 9);
    }

    if (execution.startedAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 10, toMs(execution.startedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 10);
    }

    if (execution.completedAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 11, toMs(execution.completedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 11);
    }

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            std::string("sqlite save execution failed: ") + sqlite3_errmsg(db_.handle())
        );
    }
}

std::optional<WorkflowExecution>
SQLiteWorkflowExecutionStore::find(const std::string& workflowExecutionId) const
{
    validateExecutionId(workflowExecutionId);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(), "SELECT workflow_execution_id, workflow_name, workflow_version, status, "
                      "       current_step_name, input_json, state_json, current_step_attempt, "
                      "       failure_reason, started_at_ms, completed_at_ms "
                      "FROM workflow_executions "
                      "WHERE workflow_execution_id = ?"
    );

    sqlite3_bind_text(stmt.get(), 1, workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    return rowToExecution(stmt.get());
}

void SQLiteWorkflowExecutionStore::update(const WorkflowExecution& execution)
{
    validateExecutionId(execution.workflowExecutionId);

    const auto inputJson = json::stringify(execution.input);
    const auto stateJson = json::stringify(execution.state);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(), "UPDATE workflow_executions SET "
                      "  workflow_name = ?, workflow_version = ?, status = ?, "
                      "  current_step_name = ?, input_json = ?, state_json = ?, "
                      "  current_step_attempt = ?, failure_reason = ?, "
                      "  started_at_ms = ?, completed_at_ms = ? "
                      "WHERE workflow_execution_id = ?"
    );

    sqlite3_bind_text(stmt.get(), 1, execution.workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, execution.workflowVersion);
    sqlite3_bind_int(stmt.get(), 3, toInt(execution.status));
    sqlite3_bind_text(stmt.get(), 4, execution.currentStepName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 5, inputJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, stateJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 7, execution.currentStepAttempt);

    if (execution.failureReason.has_value())
    {
        sqlite3_bind_text(
            stmt.get(), 8, execution.failureReason.value().c_str(), -1, SQLITE_TRANSIENT
        );
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 8);
    }

    if (execution.startedAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 9, toMs(execution.startedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 9);
    }

    if (execution.completedAt.has_value())
    {
        sqlite3_bind_int64(stmt.get(), 10, toMs(execution.completedAt.value()));
    }
    else
    {
        sqlite3_bind_null(stmt.get(), 10);
    }

    sqlite3_bind_text(stmt.get(), 11, execution.workflowExecutionId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            std::string("sqlite update execution failed: ") + sqlite3_errmsg(db_.handle())
        );
    }

    if (sqlite3_changes(db_.handle()) == 0)
    {
        throw std::runtime_error(
            "cannot update missing workflow execution: " + execution.workflowExecutionId
        );
    }
}

} // namespace workflow::backend::sqlite
