![wf logo and project overview](images/wf.png)
# wf

`wf` is a C++20 workflow framework for defining, validating, storing, and orchestrating workflow executions.

It currently provides:

- JSON value and parsing through the sibling `mt` library
- workflow definition parsing and validation
- workflow execution orchestration
- worker-oriented step execution
- atomic poll-and-claim semantics
- lease-based step ownership with automatic sweep
- keep-alive support for claimed steps
- optimistic concurrency control through `mt::TransactionProvider`
- `startedAt`/`completedAt` timestamps on executions
- workflow and step cancellation
- `mt::Table`-backed workflow definition, execution, and step rows
- private workflow row mappings under `src/tables/`
- in-memory and SQLite persistence through `mt` backends
- HTTP REST API server (`WorkflowHttpServer`) with OpenAPI 3.1 spec
- RFC 9457 Problem Details error responses
- ISO 8601 timestamps at API and table JSON boundaries
- `wf` CLI binary (`validate`, `parse`, `serve`, `register`, `list`, `start`, `get`, `cancel`)
- Catch2 unit tests
- Makefile-based build using `clang++`
- `-MMD -MP` header dependency tracking
- formatting with `clang-format`
- PlantUML architecture and sequence diagrams

The project is intentionally lightweight and dependency-minimal. Vendored third-party code:

- Catch2 amalgamated test runner (`third_party/catch2/`)
- cpp-httplib single-header HTTP library v0.18.5 (`third_party/httplib/`)

Non-vendored dependency:

- `mt` from https://github.com/mhendric1436/mt, expected at `$(HOME)/repos/mt` by the Makefile, provides JSON, typed tables, transactions, OCC, and memory/SQLite backends.

## Short description

C++20 workflow framework with workflow definition validation, `mt` table-backed storage, OCC-based orchestration, HTTP transport, and worker-oriented step execution.

## Repository layout

```text
wf/
├── Makefile
├── .clang-format
├── .gitignore
├── README.md
├── api/
│   └── openapi.yaml
├── cmd/
│   └── main.cpp
├── docs/
│   ├── wf-architecture.puml
│   └── wf-workflow-execution-sequence.puml
├── examples/
│   └── order-processing-workflow.json
├── include/
│   └── wf/
│       ├── duration.hpp
│       ├── workflow_definition.hpp
│       ├── workflow_execution.hpp
│       ├── workflow_step_execution.hpp
│       ├── workflow_logic.hpp
│       ├── workflow_orchestrator.hpp
│       ├── workflow_json.hpp
│       ├── workflow_service.hpp
│       ├── workflow_client.hpp
│       ├── workflow_transport.hpp
│       ├── workflow_worker.hpp
│       ├── workflow_worker_pool.hpp
│       ├── http/
│       │   └── workflow_http_server.hpp
│       ├── logic/
│       │   └── step_output_routing_logic.hpp
│       └── transport/
│           ├── http_transport.hpp
│           └── in_process_transport.hpp
├── src/
│   ├── duration.cpp
│   ├── workflow_json.cpp
│   ├── workflow_orchestrator.cpp
│   ├── workflow_service.cpp
│   ├── workflow_client.cpp
│   ├── workflow_worker.cpp
│   ├── workflow_worker_pool.cpp
│   ├── http/
│   │   └── workflow_http_server.cpp
│   ├── logic/
│   │   └── step_output_routing_logic.cpp
│   ├── tables/
│   │   ├── generated/
│   │   │   ├── workflow_definition_row.hpp
│   │   │   ├── workflow_execution_row.hpp
│   │   │   └── workflow_step_execution_row.hpp
│   │   └── schemas/
│   │       ├── workflow_definition.mt.json
│   │       ├── workflow_execution.mt.json
│   │       └── workflow_step_execution.mt.json
│   └── transport/
│       ├── http_transport.cpp
│       └── in_process_transport.cpp
├── tests/
│   ├── duration_tests.cpp
│   ├── workflow_parser_tests.cpp
│   ├── workflow_orchestrator_tests.cpp
│   ├── workflow_service_tests.cpp
│   ├── workflow_client_tests.cpp
│   ├── workflow_worker_tests.cpp
│   ├── workflow_worker_pool_tests.cpp
│   ├── e2e/
│   │   └── workflow_e2e_tests.cpp
│   ├── http/
│   │   └── workflow_http_server_tests.cpp
│   ├── logic/
│   │   └── step_output_routing_logic_tests.cpp
│   └── transport/
│       └── http_transport_tests.cpp
└── third_party/
    ├── catch2/
    │   ├── catch_amalgamated.cpp
    │   └── catch_amalgamated.hpp
    └── httplib/
        └── httplib.h
```

## Workflow definition

A workflow definition describes the allowed steps in a workflow and declares the first step.

Example:

```json
{
  "workflowName": "order-processing",
  "workflowVersion": 1,
  "startWorkflowStepName": "validate-order",
  "expectedExecutionTime": "PT5M",
  "steps": [
    {
      "name": "validate-order",
      "expectedExecutionTime": "PT10S",
      "maxRetries": 2
    },
    {
      "name": "charge-payment",
      "expectedExecutionTime": "PT30S",
      "maxRetries": 3
    },
    {
      "name": "reserve-inventory",
      "expectedExecutionTime": "PT15S",
      "maxRetries": 2
    },
    {
      "name": "fulfill-order",
      "expectedExecutionTime": "PT2M",
      "maxRetries": 1
    },
    {
      "name": "send-confirmation",
      "expectedExecutionTime": "PT10S",
      "maxRetries": 3
    }
  ]
}
```

A workflow definition is identified by:

```text
workflowName + workflowVersion
```

The `startWorkflowStepName` is part of the workflow definition content and must match one of the declared step names.

## Workflow step fields

Each workflow step currently has these defined fields:

```json
{
  "name": "validate-order",
  "expectedExecutionTime": "PT30S",
  "maxRetries": 2
}
```

The parser and model preserve additional step-level fields for future extension.

These fields are intentionally not part of the workflow definition:

```text
nextStep
service
type
```

Workflow transition logic is external to the workflow definition. Runtime business logic decides the next step through `WorkflowLogic`.

## Validation rules

The parser/validator currently enforces:

- top-level JSON value must be an object
- `workflowName` is required
- `workflowName` must match `^[A-Za-z][A-Za-z0-9_-]*$`
- `workflowVersion` is required
- `workflowVersion` must be an integer greater than or equal to `1`
- `startWorkflowStepName` is required
- top-level `expectedExecutionTime` is required
- duration fields must use supported ISO-8601 duration strings, such as `PT30S`, `PT5M`, `PT1H`
- `steps` is required
- `steps` must contain at least one step
- each step must have a `name`
- step names must be unique within the workflow definition
- `startWorkflowStepName` must match one of the step names
- step `maxRetries`, when present, must be a non-negative integer
- top-level additional fields are rejected
- step-level additional fields are preserved

## JSON support

`wf` uses the sibling `mt` library's JSON value and parser instead of `nlohmann/json`.

Files:

```text
$(HOME)/repos/mt/include/mt/json.hpp
$(HOME)/repos/mt/include/mt/json_parser.hpp
include/wf/workflow_json.hpp
src/workflow_json.cpp
```

The parser supports:

- objects
- arrays
- strings
- integers
- floating-point numbers
- booleans
- null

`wf` keeps workflow-specific JSON conversion, validation, and ISO 8601 timestamp serialization in `workflow_json`.

## Duration utility

Lease durations are derived from workflow step metadata.

Files:

```text
include/wf/duration.hpp
src/duration.cpp
tests/duration_tests.cpp
```

Main APIs:

```cpp
std::chrono::seconds parseIso8601DurationToSeconds(const std::string& value);

std::chrono::seconds calculateLeaseDuration(const std::string& expectedExecutionTime);
```

Current supported duration subset:

```text
P1D
PT30S
PT5M
PT1H
PT1H2M3S
P1DT2H3M4S
```

Unsupported examples include:

```text
P1Y
P1W
PT1.5S
```

Lease calculation rule:

```text
leaseDuration = WorkflowStep.expectedExecutionTime / 3
```

A minimum lease duration of one second is enforced.

Examples:

```text
PT30S -> 10 seconds
PT3M  -> 60 seconds
PT1H  -> 1200 seconds
PT1S  -> 1 second minimum
```

## Workflow service architecture

The main dependency structure is:

```text
WorkflowService
└── WorkflowOrchestrator
    ├── mt::Database
    ├── mt::TransactionProvider
    ├── mt::Table<WorkflowDefinitionRow, WorkflowDefinitionRowMapping>
    ├── mt::Table<WorkflowExecutionRow, WorkflowExecutionRowMapping>
    ├── mt::Table<WorkflowStepExecutionRow, WorkflowStepExecutionRowMapping>
    └── WorkflowLogic
```

`WorkflowService` depends only on `WorkflowOrchestrator`.

`WorkflowOrchestrator` coordinates:

- workflow definitions
- workflow executions
- workflow step executions
- workflow business logic

The workflow row mappings are private implementation details under:

```text
src/tables/
```

`WorkflowOrchestrator` accepts an `mt::Database&` and constructs its typed tables internally. This keeps the public wf API independent of the private mapping types while still using `mt::Table` as the storage boundary.

Typical in-process setup:

```cpp
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "wf/logic/step_output_routing_logic.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"

auto backend = std::make_shared<mt::backends::memory::MemoryBackend>();
mt::Database database(backend);

workflow::logic::StepOutputRoutingLogic logic;
workflow::WorkflowOrchestrator orchestrator(database, logic);
workflow::WorkflowService service(orchestrator);
```

## Shared mt transactions

`WorkflowOrchestrator` also exposes overloads that accept a caller-owned `mt::Transaction&`.
These overloads do not create, retry, commit, or abort the transaction, which lets a single binary
coordinate wf state changes with other libraries that use the same `mt::Database`.

```cpp
mt::TransactionProvider transactions(database);

auto execution = transactions.retry(
    [&](mt::Transaction& tx)
    {
        auto started = orchestrator.startWorkflow(
            tx, "orderProcessing", 1, mt::Json(mt::Json::Object{})
        );

        queue.enqueue(tx, "workflow-started", mt::Json::object({
            {"workflowExecutionId", started.workflowExecutionId},
        }));

        return started;
    }
);
```

Use the non-transaction overloads for normal wf-only operations. Use the `mt::Transaction&`
overloads when wf must participate in a larger atomic operation owned by the caller.

## Background lease sweep

`WorkflowService` runs a background thread that periodically sweeps for expired step leases.

Constructor:

```cpp
explicit WorkflowService(
    WorkflowOrchestrator& orchestrator,
    std::chrono::seconds sweepInterval = std::chrono::seconds{30}
);
```

The sweep thread starts when `WorkflowService` is constructed and stops cleanly when it is destroyed. Shutdown is immediate — the thread wakes on a condition variable and does not wait out the full interval.

The sweep calls `WorkflowOrchestrator::sweepExpiredLeases()`, which:

1. Finds all `Running` steps whose `leaseExpiresAt` is in the past.
2. Handles the matching execution and step rows inside an `mt` transaction.
3. If the step's attempt still matches the current execution attempt, retries or fails the execution depending on remaining `maxRetries`.

## Concurrency model

`WorkflowOrchestrator` uses `mt::TransactionProvider` for optimistic concurrency control instead of wf-owned store mutexes or per-execution stripe locks.

State-changing operations such as `startWorkflow`, `pollAndClaimWorkflowSteps`, `completeStep`, `failStep`, `cancelWorkflow`, and `sweepExpiredLeases` run as `mt` transactions. When concurrent callers race on the same rows or query result sets, `mt` detects the conflict and the orchestrator retries where appropriate.

Worker and service lifecycle code still uses local mutexes and condition variables for thread shutdown and work queues, but workflow state consistency belongs to `mt`.

## Frontend and worker API split

The service API is split by caller role.

Frontend API tier:

```text
startWorkflowExecution
cancelWorkflow
getWorkflowExecution
listWorkflowDefinitions
```

Backend worker tier:

```text
pollAndClaimWorkflowSteps
keepAliveWorkflowStep
completeWorkflowStep
failWorkflowStep
```

The frontend starts and manages workflow executions. Workers perform step execution.

## Worker execution flow

```text
1. Frontend starts workflow execution.
2. Orchestrator creates WorkflowExecution with startedAt timestamp.
3. Orchestrator creates first Pending WorkflowStepExecution.
4. Worker calls pollAndClaimWorkflowSteps.
5. Orchestrator atomically claims available steps in an `mt` transaction.
6. Worker executes business work outside WorkflowService.
7. Worker periodically calls keepAliveWorkflowStep if needed.
8. Worker calls completeWorkflowStep or failWorkflowStep.
9. Orchestrator asks WorkflowLogic for the next step.
10. Orchestrator creates the next Pending step or completes the workflow with completedAt timestamp.
```

## Poll and claim

`pollAndClaimWorkflowSteps` combines polling and claiming into one operation.

This avoids a race where multiple workers poll the same pending step before any one of them claims it.

Public request shape:

```cpp
struct PollAndClaimWorkflowStepsRequest {
    std::string workflowName;
    int workflowVersion = 0;
    std::string workerId;
    std::size_t maxResults = 1;
};
```

The caller does not provide `leaseDuration`.

Internally, the orchestrator calculates lease durations from each step's `expectedExecutionTime`.
It queries and updates `WorkflowStepExecution` rows through `mt::Table` inside a transaction.

The orchestrator transactionally:

```text
1. finds matching Pending steps
2. reclaims matching Running steps whose leases have expired
3. marks the steps Running
4. assigns workerId
5. assigns leaseExpiresAt
6. sets startedAt
7. returns the claimed steps
```

Competing workers either commit non-overlapping claims or retry after an OCC conflict.

## Step leases

A running `WorkflowStepExecution` has:

```text
workerId
leaseExpiresAt
```

There is no public `leaseId`.

The lease is associated one-to-one with the stored `WorkflowStepExecution`.

A worker can only complete or fail a step while:

```text
- the step is Running
- workerId matches
- leaseExpiresAt exists
- leaseExpiresAt is still in the future
```

If the lease expires, another worker can reclaim the step via `pollAndClaimWorkflowSteps`, or the background sweep will retry or fail the execution.

## Keep alive

Workers can extend a running step lease:

```cpp
struct KeepAliveWorkflowStepRequest {
    std::string workflowExecutionId;
    std::string stepName;
    std::string workerId;
};
```

The caller does not provide lease duration.

The orchestrator:

```text
1. loads the WorkflowExecution
2. verifies the workflow is running
3. verifies stepName is the current step
4. loads the WorkflowDefinition
5. finds the current step definition
6. calculates lease duration from step.expectedExecutionTime / 3
7. loads and updates the current WorkflowStepExecution row in an mt transaction
```

The transaction validates:

```text
- step exists
- step is Running
- workerId owns the claim
- lease has not expired
```

Then it updates `leaseExpiresAt`.

## Execution models

`WorkflowExecution` tracks the overall workflow execution.

Key fields:

```text
workflowExecutionId
workflowName
workflowVersion
status
currentStepName
input
state
currentStepAttempt
failureReason
startedAt
completedAt
```

`WorkflowStepExecution` tracks an executable unit of work.

Key fields:

```text
workflowExecutionId
workflowName
workflowVersion
stepName
attempt
status
workerId
leaseExpiresAt
failureReason
input
state
output
createdAt
startedAt
completedAt
```

Workflow execution statuses:

```text
Running
Completed
Failed
Canceled
```

Step execution statuses:

```text
Pending
Running
Completed
Failed
Canceled
```

`completedAt` is set on `WorkflowExecution` when the workflow reaches `Completed`, `Failed`, or `Canceled`. `startedAt` is set when the workflow execution is created.

## mt tables and persistence

Workflow state is stored as typed `mt::Table` rows. The private row schemas and generated mappings live under:

```text
src/tables/schemas/*.mt.json
src/tables/generated/*_row.hpp
```

The generated mappings define table names, row keys, JSON serialization, and JSON indexes used by `mt` backends. The logical tables are:

```text
workflow_definitions
workflow_executions
workflow_step_executions
```

Row keys:

```text
WorkflowDefinition      workflowName:workflowVersion
WorkflowExecution       workflowExecutionId
WorkflowStepExecution   workflowExecutionId:stepName:attempt
```

`WorkflowOrchestrator` constructs these tables from an `mt::Database&` and performs state changes with `mt::TransactionProvider`. `mt` provides OCC validation for both row reads and predicate reads, which is what makes poll-and-claim, completion, failure, cancellation, and lease sweep concurrency-safe.

The `wf` CLI `serve` command uses the `mt` SQLite backend:

```bash
wf serve --port 8080 --db /path/to/wf.db
```

Tests primarily use the `mt` in-memory backend for orchestrator, service, worker, HTTP, and transport behavior.

## HTTP server

`WorkflowHttpServer` exposes `WorkflowService` as an HTTP REST API. It uses cpp-httplib (single-header, vendored at `third_party/httplib/httplib.h`) behind a pimpl to keep the large header out of public interfaces.

Header:

```text
include/wf/http/workflow_http_server.hpp
```

Source:

```text
src/http/workflow_http_server.cpp
```

Tests:

```text
tests/http/workflow_http_server_tests.cpp
```

### Instantiation

```cpp
#include "wf/http/workflow_http_server.hpp"

WorkflowHttpServer server(service, port);  // port 0 = any available port
int actualPort = server.bind();
server.start();  // blocks; call from a dedicated thread
// ...
server.stop();
```

`bind()` returns the actual bound port, which is useful when using port 0 in tests. `start()` uses `listen_after_bind()` if `bind()` was already called, or `listen()` otherwise.

### Endpoints

All paths are under the `/v1` prefix.

```text
POST   /v1/workflow-definitions/validate
GET    /v1/workflow-definitions
POST   /v1/workflow-definitions
POST   /v1/workflow-executions
GET    /v1/workflow-executions/{id}
DELETE /v1/workflow-executions/{id}
POST   /v1/workflow-step-executions/poll-and-claim
POST   /v1/workflow-step-executions/keep-alive
POST   /v1/workflow-step-executions/complete
POST   /v1/workflow-step-executions/fail
```

### Timestamps

All timestamps are ISO 8601 strings in UTC (e.g. `2026-05-05T14:32:00.123456Z`). The internal C++ model uses `std::chrono::system_clock::time_point`; conversion happens through `workflow_json` for HTTP responses and `mt` row JSON.

### Errors

Errors use RFC 9457 Problem Details with content type `application/problem+json`.

```json
{
  "type": "not-found",
  "title": "Not Found",
  "status": 404,
  "detail": "workflow execution not found: wfexec-42"
}
```

Exception-to-status mapping:

```text
JsonParseError          → 400 invalid-argument
std::invalid_argument   → 400 invalid-argument
std::runtime_error (message contains "not found") → 404 not-found
std::runtime_error (other)  → 409 conflict
std::exception (other)      → 500 internal-error
```

## OpenAPI specification

The full API contract is specified in:

```text
api/openapi.yaml
```

The spec is OpenAPI 3.1.0 and covers all ten endpoints, all request and response schemas, and all error responses.

## Future backend direction

New persistence backends should be added to `mt`, not to wf-specific store interfaces. `wf` should continue to depend on:

```text
mt::Database
mt::Table
mt::TransactionProvider
```

This keeps workflow orchestration independent of any particular durable backend while preserving a single OCC model across memory, SQLite, PostgreSQL, or future `mt` backends.

## wf CLI

The `wf` binary provides command-line access to the parser and validator.

```text
build/bin/wf
```

Usage:

```bash
wf <command> [args]
```

Commands:

```text
validate <file>                    validate a workflow definition JSON file
parse <file>                       parse and display a workflow definition
serve [--port <n>] --db <path>     start the HTTP server using mt SQLite storage
register [--server <url>] <file>   register a workflow definition
list [--server <url>]              list registered workflow definitions
start [--server <url>] [--input <json>] <name> <version>
get [--server <url>] <id>
cancel [--server <url>] <id>
```

### validate

Validates a workflow definition JSON file and exits with status `0` if valid, `1` if not.

```bash
wf validate examples/order-processing-workflow.json
# valid

wf validate bad.json
# invalid:
#   - workflowName is required
```

### parse

Parses and displays the fields of a workflow definition.

```bash
wf parse examples/order-processing-workflow.json
# name:    order-processing
# version: 1
# time:    PT5M
# start:   validate-order
# steps:
#   validate-order   time=PT10S  maxRetries=2
#   charge-payment   time=PT30S  maxRetries=3
#   reserve-inventory  time=PT15S  maxRetries=2
#   fulfill-order    time=PT2M   maxRetries=1
#   send-confirmation  time=PT10S  maxRetries=3
```

## Build requirements

Required:

- C++20 compiler
- `clang++`
- `clang-format`
- `make`
- `mt` checkout from https://github.com/mhendric1436/mt at `$(HOME)/repos/mt`
- SQLite development library for the `wf` CLI server path and mt SQLite backend

Optional:

- PlantUML, for rendering diagrams
- Graphviz, depending on the PlantUML installation and diagram type

On macOS:

```bash
brew install llvm clang-format plantuml graphviz
```

Depending on your shell configuration, Homebrew's LLVM tools may not be first on your `PATH`.

## Test dependency setup

The project expects Catch2 amalgamated files under `third_party/catch2`.

```bash
mkdir -p third_party/catch2

curl -L \
  https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.hpp \
  -o third_party/catch2/catch_amalgamated.hpp

curl -L \
  https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.cpp \
  -o third_party/catch2/catch_amalgamated.cpp
```

## Build and test

Build and run tests, then build the CLI:

```bash
make
```

Build and run tests only:

```bash
make test
```

Build only the static library:

```bash
make build
```

Build only the CLI binary:

```bash
make cli
```

Generated outputs:

```text
build/libwf.a        static library
build/bin/wf_tests   test binary
build/bin/wf         CLI binary
```

## Wildcard Makefile

The Makefile discovers files automatically:

```text
src/**/*.cpp
src/tables/**/*.hpp
src/tables/schemas/*.mt.json
tests/**/*.cpp
cmd/**/*.cpp
include/**/*.hpp
```

The CLI target also compiles the `mt` SQLite backend sources from `$(HOME)/repos/mt/src/backends/sqlite` so `wf serve --db ...` can use durable `mt` storage.

Header dependency tracking uses `-MMD -MP`, so changes to included headers trigger recompilation of affected object files without manual dependency management.

Useful target:

```bash
make print-files
```

This prints discovered source, test, cmd, and header files.

## Formatting

Format source and header files:

```bash
make format
```

Check formatting without modifying files:

```bash
make format-check
```

## Diagrams

PlantUML source files live in:

```text
docs/
```

Generate PNG files from all `docs/*.puml` files:

```bash
make docs-png
```

This converts files such as:

```text
docs/wf-architecture.puml
docs/wf-workflow-execution-sequence.puml
```

into:

```text
docs/wf-architecture.png
docs/wf-workflow-execution-sequence.png
```

## Clean

Remove build outputs and generated diagram PNG files:

```bash
make clean
```

## Current Makefile targets

```text
make              Build and run tests, then build CLI
make build        Build static library only
make test         Build and run tests
make cli          Build the wf CLI binary
make codegen      Generate private mt row and mapping headers
make codegen-check Verify generated mt row headers are current
make format       Format source and header files with clang-format
make format-check Check formatting without modifying files
make docs-png     Generate PNG diagrams from docs/*.puml
make print-files  Show discovered source, test, cmd, and header files
make clean        Remove build outputs and generated docs/*.png
make help         Show available targets
```

## Current development status

Implemented:

- `mt` JSON integration
- duration utility
- workflow definition model
- workflow definition parser and validator
- workflow execution model with `startedAt`/`completedAt` timestamps
- workflow step execution model with `createdAt`/`startedAt`/`completedAt` timestamps
- workflow service API surface
- workflow orchestrator API surface
- private `mt` row mappings under `src/tables`
- `mt::Table` storage for workflow definitions, executions, and step executions
- `mt::TransactionProvider`-backed workflow state mutations
- OCC-based atomic `pollAndClaim`
- internal lease duration calculation from `WorkflowStep.expectedExecutionTime`
- lease expiration and reclaim behavior
- `keepAliveWorkflowStep`
- background lease sweep thread in `WorkflowService`
- workflow and step cancellation
- `WorkflowClient`
- in-process and HTTP transports
- `WorkflowWorker`
- `WorkflowWorkerPool`
- HTTP REST API server (`WorkflowHttpServer`) with all ten endpoints
- OpenAPI 3.1 specification (`api/openapi.yaml`)
- RFC 9457 Problem Details error responses
- ISO 8601 timestamps at HTTP and table JSON boundaries
- `wf` CLI (`validate`, `parse`, `serve`, `register`, `list`, `start`, `get`, `cancel`)
- example workflow definition (`examples/order-processing-workflow.json`)
- `-MMD -MP` header dependency tracking
- parser tests
- duration tests
- lease tests
- orchestrator tests
- service tests
- HTTP server tests
- transport tests
- worker tests
- e2e tests
- PlantUML architecture and sequence diagrams
- wildcard Makefile

## Recommended next steps

1. Update PlantUML diagrams to reflect the current architecture: `mt::Database`, typed tables, OCC transactions, HTTP server, worker pool, and background sweep.

2. Add scripted `WorkflowLogic` driven from workflow definition JSON if wf should support simple declarative routing in addition to application-owned `WorkflowLogic`.

3. Add focused tests for conflict/retry behavior under `mt::TransactionConflict`.
