#pragma once

#include "wf/workflow_execution.hpp"
#include "wf/workflow_step_execution.hpp"

#include <chrono>
#include <cstdint>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace workflow::backend::sqlite
{

class Stmt
{
  public:
    Stmt(sqlite3* db, const std::string& sql)
    {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(
                std::string("sqlite prepare failed: ") + sqlite3_errmsg(db)
            );
        }
    }

    ~Stmt() { sqlite3_finalize(stmt_); }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    sqlite3_stmt* get() const { return stmt_; }

  private:
    sqlite3_stmt* stmt_ = nullptr;
};

inline void exec(sqlite3* db, const char* sql)
{
    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK)
    {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error(std::string("sqlite exec failed: ") + msg);
    }
}

inline int64_t toMs(std::chrono::system_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

inline std::chrono::system_clock::time_point fromMs(int64_t ms)
{
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

inline int toInt(WorkflowExecutionStatus s)
{
    switch (s)
    {
    case WorkflowExecutionStatus::Running:
        return 0;
    case WorkflowExecutionStatus::Completed:
        return 1;
    case WorkflowExecutionStatus::Failed:
        return 2;
    case WorkflowExecutionStatus::Canceled:
        return 3;
    }
    return 0;
}

inline WorkflowExecutionStatus toExecutionStatus(int v)
{
    switch (v)
    {
    case 0:
        return WorkflowExecutionStatus::Running;
    case 1:
        return WorkflowExecutionStatus::Completed;
    case 2:
        return WorkflowExecutionStatus::Failed;
    case 3:
        return WorkflowExecutionStatus::Canceled;
    default:
        return WorkflowExecutionStatus::Failed;
    }
}

inline int toInt(StepExecutionStatus s)
{
    switch (s)
    {
    case StepExecutionStatus::Pending:
        return 0;
    case StepExecutionStatus::Running:
        return 1;
    case StepExecutionStatus::Completed:
        return 2;
    case StepExecutionStatus::Failed:
        return 3;
    case StepExecutionStatus::Canceled:
        return 4;
    }
    return 0;
}

inline StepExecutionStatus toStepStatus(int v)
{
    switch (v)
    {
    case 0:
        return StepExecutionStatus::Pending;
    case 1:
        return StepExecutionStatus::Running;
    case 2:
        return StepExecutionStatus::Completed;
    case 3:
        return StepExecutionStatus::Failed;
    case 4:
        return StepExecutionStatus::Canceled;
    default:
        return StepExecutionStatus::Failed;
    }
}

} // namespace workflow::backend::sqlite
