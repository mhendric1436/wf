#include "wf/backend/sqlite/sqlite_workflow_definition_store.hpp"

#include "sqlite_helpers.hpp"
#include "wf/json.hpp"

#include <stdexcept>

namespace workflow::backend::sqlite
{

namespace
{

void validateKey(
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

std::string stepsToJson(const std::vector<WorkflowStep>& steps)
{
    json::Value::Array arr;

    for (const auto& step : steps)
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

        arr.push_back(json::Value(std::move(obj)));
    }

    return json::stringify(json::Value(std::move(arr)));
}

std::vector<WorkflowStep> stepsFromJson(const std::string& text)
{
    const auto arr = json::parse(text);
    std::vector<WorkflowStep> steps;

    for (const auto& item : arr.asArray())
    {
        WorkflowStep step;
        step.name = item["name"].asString();

        if (item.contains("expectedExecutionTime"))
        {
            step.expectedExecutionTime = item["expectedExecutionTime"].asString();
        }

        if (item.contains("maxRetries"))
        {
            step.maxRetries = item["maxRetries"].asInt();
        }

        for (const auto& [k, v] : item.asObject())
        {
            if (k != "name" && k != "expectedExecutionTime" && k != "maxRetries")
            {
                step.additionalFields[k] = v;
            }
        }

        steps.push_back(std::move(step));
    }

    return steps;
}

WorkflowDefinition rowToDefinition(sqlite3_stmt* stmt)
{
    WorkflowDefinition def;
    def.workflowName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    def.workflowVersion = sqlite3_column_int(stmt, 1);
    def.startWorkflowStepName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    def.expectedExecutionTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    def.steps = stepsFromJson(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
    return def;
}

} // namespace

SQLiteWorkflowDefinitionStore::SQLiteWorkflowDefinitionStore(SQLiteDatabase& db)
    : db_(db)
{
}

void SQLiteWorkflowDefinitionStore::save(const WorkflowDefinition& definition)
{
    validateKey(definition.workflowName, definition.workflowVersion);

    const auto stepsJson = stepsToJson(definition.steps);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(), "INSERT OR REPLACE INTO workflow_definitions "
                      "(workflow_name, workflow_version, start_workflow_step_name, "
                      " expected_execution_time, steps_json) "
                      "VALUES (?, ?, ?, ?, ?)"
    );

    sqlite3_bind_text(stmt.get(), 1, definition.workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, definition.workflowVersion);
    sqlite3_bind_text(
        stmt.get(), 3, definition.startWorkflowStepName.c_str(), -1, SQLITE_TRANSIENT
    );
    sqlite3_bind_text(
        stmt.get(), 4, definition.expectedExecutionTime.c_str(), -1, SQLITE_TRANSIENT
    );
    sqlite3_bind_text(stmt.get(), 5, stepsJson.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            std::string("sqlite save definition failed: ") + sqlite3_errmsg(db_.handle())
        );
    }
}

std::optional<WorkflowDefinition> SQLiteWorkflowDefinitionStore::find(
    const std::string& workflowName,
    int workflowVersion
) const
{
    validateKey(workflowName, workflowVersion);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(), "SELECT workflow_name, workflow_version, start_workflow_step_name, "
                      "       expected_execution_time, steps_json "
                      "FROM workflow_definitions "
                      "WHERE workflow_name = ? AND workflow_version = ?"
    );

    sqlite3_bind_text(stmt.get(), 1, workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, workflowVersion);

    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    return rowToDefinition(stmt.get());
}

std::vector<WorkflowDefinitionKey> SQLiteWorkflowDefinitionStore::list() const
{
    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(), "SELECT workflow_name, workflow_version "
                      "FROM workflow_definitions "
                      "ORDER BY workflow_name, workflow_version"
    );

    std::vector<WorkflowDefinitionKey> keys;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW)
    {
        WorkflowDefinitionKey key;
        key.workflowName = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        key.workflowVersion = sqlite3_column_int(stmt.get(), 1);
        keys.push_back(std::move(key));
    }

    return keys;
}

void SQLiteWorkflowDefinitionStore::remove(
    const std::string& workflowName,
    int workflowVersion
)
{
    validateKey(workflowName, workflowVersion);

    std::lock_guard<std::mutex> lock(db_.mutex());

    Stmt stmt(
        db_.handle(),
        "DELETE FROM workflow_definitions WHERE workflow_name = ? AND workflow_version = ?"
    );

    sqlite3_bind_text(stmt.get(), 1, workflowName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, workflowVersion);

    sqlite3_step(stmt.get());
}

} // namespace workflow::backend::sqlite
