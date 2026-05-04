![wf logo and project overview](images/wf.png)
# wf

`wf` is a C++20 workflow framework for defining, validating, storing, and orchestrating workflow executions.

It currently provides:

- a custom JSON parser
- workflow definition parsing and validation
- workflow execution orchestration
- worker-oriented step execution
- atomic poll-and-claim semantics
- lease-based step ownership
- keep-alive support for claimed steps
- pluggable store interfaces
- in-memory backend implementations
- Catch2 unit tests
- Makefile-based build using `clang++`
- formatting with `clang-format`
- PlantUML architecture and sequence diagrams

The project is intentionally lightweight and dependency-minimal. The only third-party code expected in the repo is Catch2's amalgamated test runner.

## Short description

C++20 workflow framework with custom JSON parsing, workflow definition validation, pluggable stores, in-memory backend, and worker-oriented step orchestration.

## Repository layout

```text
wf/
├── Makefile
├── .clang-format
├── .gitignore
├── README.md
├── docs/
│   ├── wf-architecture.puml
│   └── wf-workflow-execution-sequence.puml
├── include/
│   └── wf/
│       ├── duration.hpp
│       ├── json.hpp
│       ├── workflow_definition.hpp
│       ├── workflow_execution.hpp
│       ├── workflow_step_execution.hpp
│       ├── workflow_logic.hpp
│       ├── workflow_orchestrator.hpp
│       ├── workflow_parser.hpp
│       ├── workflow_service.hpp
│       ├── store/
│       │   ├── workflow_definition_store.hpp
│       │   ├── workflow_execution_store.hpp
│       │   └── workflow_step_execution_store.hpp
│       └── backend/
│           └── memory/
│               ├── in_memory_workflow_definition_store.hpp
│               ├── in_memory_workflow_execution_store.hpp
│               └── in_memory_workflow_step_execution_store.hpp
├── src/
│   ├── duration.cpp
│   ├── json.cpp
│   ├── workflow_parser.cpp
│   ├── workflow_orchestrator.cpp
│   ├── workflow_service.cpp
│   └── backend/
│       └── memory/
│           ├── in_memory_workflow_definition_store.cpp
│           ├── in_memory_workflow_execution_store.cpp
│           └── in_memory_workflow_step_execution_store.cpp
├── tests/
│   ├── duration_tests.cpp
│   ├── workflow_parser_tests.cpp
│   ├── workflow_orchestrator_tests.cpp
│   └── backend/
│       └── memory/
│           ├── in_memory_workflow_definition_store_tests.cpp
│           ├── in_memory_workflow_execution_store_tests.cpp
│           ├── in_memory_workflow_step_execution_store_tests.cpp
│           └── in_memory_workflow_step_execution_store_lease_tests.cpp
└── third_party/
    └── catch2/
        ├── catch_amalgamated.cpp
        └── catch_amalgamated.hpp
```

## Workflow definition

A workflow definition describes the allowed steps in a workflow and declares the first step.

Example:

```json
{
  "workflowName": "orderProcessing",
  "workflowVersion": 1,
  "startWorkflowStepName": "validateOrder",
  "expectedExecutionTime": "PT10M",
  "steps": [
    {
      "name": "validateOrder",
      "expectedExecutionTime": "PT30S",
      "maxRetries": 2
    },
    {
      "name": "chargePayment",
      "expectedExecutionTime": "PT2M",
      "maxRetries": 3
    },
    {
      "name": "shipOrder",
      "expectedExecutionTime": "PT5M",
      "maxRetries": 1
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
  "name": "validateOrder",
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

## Custom JSON parser

The project uses a custom JSON parser instead of `nlohmann/json`.

Files:

```text
include/wf/json.hpp
src/json.cpp
```

The parser supports:

- objects
- arrays
- strings
- integers
- floating-point numbers
- booleans
- null

Unicode escape sequences such as `\uXXXX` are intentionally not supported yet. The parser reports them as parse errors instead of silently producing incorrect values.

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
    ├── WorkflowDefinitionStore
    ├── WorkflowExecutionStore
    ├── WorkflowStepExecutionStore
    └── WorkflowLogic
```

`WorkflowService` depends only on `WorkflowOrchestrator`.

`WorkflowOrchestrator` coordinates:

- workflow definitions
- workflow executions
- workflow step executions
- workflow business logic

## Frontend and worker API split

The service API is split by caller role.

Frontend API tier:

```text
startWorkflowExecution
```

Backend worker tier:

```text
pollAndClaimWorkflowSteps
keepAliveWorkflowStep
completeWorkflowStep
failWorkflowStep
```

The frontend starts workflow executions only. Workers perform step execution.

## Worker execution flow

```text
1. Frontend starts workflow execution.
2. Orchestrator creates WorkflowExecution.
3. Orchestrator creates first Pending WorkflowStepExecution.
4. Worker calls pollAndClaimWorkflowSteps.
5. Store atomically claims available steps.
6. Worker executes business work outside WorkflowService.
7. Worker periodically calls keepAliveWorkflowStep if needed.
8. Worker calls completeWorkflowStep or failWorkflowStep.
9. Orchestrator asks WorkflowLogic for the next step.
10. Orchestrator creates the next Pending step or completes the workflow.
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

Store-level operation:

```cpp
pollAndClaim(
    workflowName,
    workflowVersion,
    workerId,
    maxResults,
    leaseDurationsByStepName
)
```

The store atomically:

```text
1. finds matching Pending steps
2. reclaims matching Claimed steps whose leases have expired
3. marks the steps Claimed
4. assigns workerId
5. assigns leaseExpiresAt
6. returns the claimed steps
```

## Step leases

A claimed `WorkflowStepExecution` has:

```text
workerId
leaseExpiresAt
```

There is no public `leaseId`.

The lease is associated one-to-one with the stored `WorkflowStepExecution`.

A worker can only complete or fail a step while:

```text
- the step is Claimed
- workerId matches
- leaseExpiresAt exists
- leaseExpiresAt is still in the future
```

If the lease expires, another worker can reclaim the step with `pollAndClaimWorkflowSteps`.

## Keep alive

Workers can extend a claimed step lease:

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
7. delegates keepAlive to WorkflowStepExecutionStore
```

The store validates:

```text
- step exists
- step is Claimed
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
```

Step execution statuses:

```text
Pending
Claimed
Running
Completed
Failed
Canceled
```

## Store interfaces

Store interfaces live under:

```text
include/wf/store/
```

Current interfaces:

```text
workflow_definition_store.hpp
workflow_execution_store.hpp
workflow_step_execution_store.hpp
```

`WorkflowDefinitionStore` supports:

```text
save
find
list
remove
```

`WorkflowExecutionStore` supports:

```text
save
find
update
```

`WorkflowStepExecutionStore` supports:

```text
save
find
pollAndClaim
keepAlive
update
remove
```

## In-memory backend

The initial backend is in-memory.

Headers:

```text
include/wf/backend/memory/in_memory_workflow_definition_store.hpp
include/wf/backend/memory/in_memory_workflow_execution_store.hpp
include/wf/backend/memory/in_memory_workflow_step_execution_store.hpp
```

Sources:

```text
src/backend/memory/in_memory_workflow_definition_store.cpp
src/backend/memory/in_memory_workflow_execution_store.cpp
src/backend/memory/in_memory_workflow_step_execution_store.cpp
```

Tests:

```text
tests/backend/memory/in_memory_workflow_definition_store_tests.cpp
tests/backend/memory/in_memory_workflow_execution_store_tests.cpp
tests/backend/memory/in_memory_workflow_step_execution_store_tests.cpp
tests/backend/memory/in_memory_workflow_step_execution_store_lease_tests.cpp
```

The in-memory step execution store uses `std::mutex` to make `pollAndClaim` and `keepAlive` atomic.

## Future backend layout

Additional backends should follow this layout:

```text
include/wf/backend/sqlite/
src/backend/sqlite/

include/wf/backend/postgres/
src/backend/postgres/

include/wf/backend/rocksdb/
src/backend/rocksdb/
```

The core framework should depend on store interfaces, not backend-specific classes.

## Build requirements

Required:

- C++20 compiler
- `clang++`
- `clang-format`
- `make`

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

Build and run tests:

```bash
make
```

Equivalent explicit command:

```bash
make test
```

Build only the static library:

```bash
make build
```

The generated library is:

```text
build/libwf.a
```

The generated test binary is:

```text
build/bin/wf_tests
```

## Wildcard Makefile

The Makefile discovers files automatically:

```text
src/**/*.cpp
tests/**/*.cpp
include/**/*.hpp
```

Useful target:

```bash
make print-files
```

This prints discovered source, test, and header files.

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
make              Build and run tests
make build        Build static library only
make test         Build and run tests
make format       Format source and header files with clang-format
make format-check Check formatting without modifying files
make docs-png     Generate PNG diagrams from docs/*.puml
make print-files  Show discovered source, test, and header files
make clean        Remove build outputs and generated docs/*.png
make help         Show available targets
```

## Current development status

Implemented:

- custom JSON parser
- duration utility
- workflow definition model
- workflow definition parser and validator
- workflow execution model
- workflow step execution model
- workflow service API surface
- workflow orchestrator API surface
- store interfaces
- in-memory definition store
- in-memory execution store
- in-memory step execution store
- atomic `pollAndClaim`
- internal lease duration calculation from `WorkflowStep.expectedExecutionTime`
- lease expiration and reclaim behavior
- `keepAliveWorkflowStep`
- parser tests
- duration tests
- memory backend tests
- lease tests
- orchestrator tests
- PlantUML architecture and sequence diagrams
- wildcard Makefile

## Recommended next steps

1. Run the current test suite:
   ```bash
   make clean
   make
   ```

2. Update PlantUML diagrams to reflect:
   - internal lease duration calculation
   - no caller-supplied lease duration
   - `keepAliveWorkflowStep`

3. Update or add service tests for:
   - start workflow execution
   - poll-and-claim through `WorkflowService`
   - keep-alive through `WorkflowService`
   - complete/fail through `WorkflowService`

4. Add example workflow definitions:
   ```text
   examples/order_processing_workflow.json
   ```

5. Add a small CLI:
   ```bash
   wf validate examples/order_processing_workflow.json
   ```

6. Add HTTP API layer:
   ```text
   POST /v1/workflow-executions
   POST /v1/workflow-step-executions/poll-and-claim
   POST /v1/workflow-step-executions/keep-alive
   POST /v1/workflow-step-executions/complete
   POST /v1/workflow-step-executions/fail
   ```

7. Add persistent backends:
   ```text
   SQLite
   RocksDB
   Postgres
   ```
