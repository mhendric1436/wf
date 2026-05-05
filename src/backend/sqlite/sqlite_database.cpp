#include "wf/backend/sqlite/sqlite_database.hpp"

#include "sqlite_helpers.hpp"

#include <stdexcept>
#include <string>

namespace workflow::backend::sqlite
{

SQLiteDatabase::SQLiteDatabase(const std::string& path)
{
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
    {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "cannot open database";
        sqlite3_close(db_);
        throw std::runtime_error("sqlite open failed: " + msg);
    }

    exec(db_, "PRAGMA journal_mode=WAL");
    exec(db_, "PRAGMA foreign_keys=OFF");

    createTables();
}

SQLiteDatabase::~SQLiteDatabase()
{
    sqlite3_close(db_);
}

sqlite3* SQLiteDatabase::handle() const
{
    return db_;
}

std::mutex& SQLiteDatabase::mutex() const
{
    return mutex_;
}

void SQLiteDatabase::createTables()
{
    exec(
        db_, "CREATE TABLE IF NOT EXISTS workflow_definitions ("
             "  workflow_name              TEXT    NOT NULL,"
             "  workflow_version           INTEGER NOT NULL,"
             "  start_workflow_step_name   TEXT    NOT NULL,"
             "  expected_execution_time    TEXT    NOT NULL,"
             "  steps_json                 TEXT    NOT NULL,"
             "  PRIMARY KEY (workflow_name, workflow_version)"
             ")"
    );

    exec(
        db_, "CREATE TABLE IF NOT EXISTS workflow_executions ("
             "  workflow_execution_id   TEXT    PRIMARY KEY,"
             "  workflow_name           TEXT    NOT NULL,"
             "  workflow_version        INTEGER NOT NULL,"
             "  status                  INTEGER NOT NULL,"
             "  current_step_name       TEXT    NOT NULL,"
             "  input_json              TEXT    NOT NULL,"
             "  state_json              TEXT    NOT NULL,"
             "  current_step_attempt    INTEGER NOT NULL,"
             "  failure_reason          TEXT,"
             "  started_at_ms           INTEGER,"
             "  completed_at_ms         INTEGER"
             ")"
    );

    exec(
        db_, "CREATE TABLE IF NOT EXISTS workflow_step_executions ("
             "  workflow_execution_id   TEXT    NOT NULL,"
             "  workflow_name           TEXT    NOT NULL,"
             "  workflow_version        INTEGER NOT NULL,"
             "  step_name               TEXT    NOT NULL,"
             "  attempt                 INTEGER NOT NULL,"
             "  status                  INTEGER NOT NULL,"
             "  worker_id               TEXT,"
             "  lease_expires_at_ms     INTEGER,"
             "  failure_reason          TEXT,"
             "  created_at_ms           INTEGER,"
             "  started_at_ms           INTEGER,"
             "  completed_at_ms         INTEGER,"
             "  input_json              TEXT    NOT NULL,"
             "  state_json              TEXT    NOT NULL,"
             "  output_json             TEXT    NOT NULL,"
             "  PRIMARY KEY (workflow_execution_id, step_name, attempt)"
             ")"
    );

    exec(
        db_, "CREATE INDEX IF NOT EXISTS idx_step_executions_poll "
             "ON workflow_step_executions (workflow_name, workflow_version, status)"
    );

    exec(
        db_, "CREATE INDEX IF NOT EXISTS idx_step_executions_sweep "
             "ON workflow_step_executions (status, lease_expires_at_ms)"
    );
}

} // namespace workflow::backend::sqlite
