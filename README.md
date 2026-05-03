# wf

`wf` is a C++20 workflow definition parser, validator, and workflow service framework.

The project currently provides:

- a custom JSON parser
- workflow definition model types
- workflow definition validation
- workflow execution model types
- workflow service/orchestrator interfaces
- in-memory backend stores
- Catch2 unit tests
- Makefile-based build using `clang++`
- formatting with `clang-format`
- PlantUML architecture and sequence diagrams

The current implementation is intentionally lightweight and dependency-minimal. The only third-party code expected in the repo is Catch2's amalgamated test runner.

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
│       ├── json.hpp
│       ├── workflow_definition.hpp
│       ├── workflow_execution.hpp
│       ├── workflow_logic.hpp
│       ├── workflow_orchestrator.hpp
│       ├── workflow_parser.hpp
│       ├── workflow_service.hpp
│       ├── store/
│       │   ├── workflow_definition_store.hpp
│       │   └── workflow_execution_store.hpp
│       └── backend/
│           └── memory/
│               ├── in_memory_workflow_definition_store.hpp
│               └── in_memory_workflow_execution_store.hpp
├── src/
│   ├── json.cpp
│   ├── workflow_parser.cpp
│   ├── workflow_orchestrator.cpp
│   ├── workflow_service.cpp
│   └── backend/
│       └── memory/
│           ├── in_memory_workflow_definition_store.cpp
│           └── in_memory_workflow_execution_store.cpp
├── tests/
│   ├── workflow_parser_tests.cpp
│   └── backend/
│       └── memory/
│           ├── in_memory_workflow_definition_store_tests.cpp
│           └── in_memory_workflow_execution_store_tests.cpp
└── third_party/
    └── catch2/
        ├── catch_amalgamated.cpp
        └── catch_amalgamated.hpp
```

## Workflow definition

A workflow definition is identified by:

- `workflowName`
- `workflowVersion`

The definition also declares:

- `startWorkflowStepName`
- top-level `expectedExecutionTime`
- a list of workflow `steps`

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

## Workflow step fields

Each workflow step currently has these defined fields:

```json
{
  "name": "validateOrder",
  "expectedExecutionTime": "PT30S",
  "maxRetries": 2
}
```

The parser and model allow additional step-level fields so the definition can be extended later.

Do not rely on these fields for transition logic:

- `nextStep`
- `service`
- `type`

Workflow transition logic is external to the workflow definition. The workflow service asks workflow business logic which step should execute next.

## Validation rules

The parser/validator currently enforces:

- top-level JSON value must be an object
- `workflowName` is required
- `workflowName` must match `^[A-Za-z][A-Za-z0-9_-]*$`
- `workflowVersion` is required
- `workflowVersion` must be an integer greater than or equal to `1`
- `startWorkflowStepName` is required
- top-level `expectedExecutionTime` is required
- execution duration fields must use ISO-8601 duration strings, such as `PT30S`, `PT5M`, `PT1H`
- `steps` is required
- `steps` must contain at least one step
- each step must have a `name`
- step names must be unique within the workflow definition
- `startWorkflowStepName` must match one of the step names
- step `maxRetries`, when present, must be a non-negative integer
- top-level additional fields are rejected
- step-level additional fields are preserved

## Custom JSON parser

The project uses its own JSON parser instead of `nlohmann/json`.

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

## Workflow service framework

The service framework is split into core interfaces and backend implementations.

Core service files:

```text
include/wf/workflow_execution.hpp
include/wf/workflow_logic.hpp
include/wf/workflow_orchestrator.hpp
include/wf/workflow_service.hpp

src/workflow_orchestrator.cpp
src/workflow_service.cpp
```

The main service operations are:

- validate workflow definition
- register workflow definition
- start workflow execution
- complete workflow step
- fail workflow step

The orchestration flow is:

1. Client starts a workflow execution through `WorkflowService`.
2. `WorkflowService` delegates to `WorkflowOrchestrator`.
3. `WorkflowOrchestrator` loads the workflow definition.
4. It creates a `WorkflowExecution` at the declared start step.
5. The execution is saved through a `WorkflowExecutionStore`.
6. When a step completes, the orchestrator asks `WorkflowLogic` for the next step decision.
7. The workflow either advances to the next step or completes.
8. When a step fails, retry behavior is governed by the step's `maxRetries`.

## Store interfaces

Store interfaces live under:

```text
include/wf/store/
```

Current interfaces:

```text
include/wf/store/workflow_definition_store.hpp
include/wf/store/workflow_execution_store.hpp
```

These interfaces isolate the core workflow framework from backend-specific persistence.

## In-memory backend

The first backend implementation is an in-memory backend.

Headers:

```text
include/wf/backend/memory/in_memory_workflow_definition_store.hpp
include/wf/backend/memory/in_memory_workflow_execution_store.hpp
```

Sources:

```text
src/backend/memory/in_memory_workflow_definition_store.cpp
src/backend/memory/in_memory_workflow_execution_store.cpp
```

Tests:

```text
tests/backend/memory/in_memory_workflow_definition_store_tests.cpp
tests/backend/memory/in_memory_workflow_execution_store_tests.cpp
```

The in-memory definition store supports:

- save
- find
- list
- remove
- clear
- size

The in-memory execution store supports:

- save
- find
- update
- remove
- clear
- size

Current backend behavior:

- definition save replaces an existing workflow with the same name/version
- execution save replaces an existing execution with the same execution ID
- execution update requires the execution to already exist
- invalid definition keys throw `std::invalid_argument`
- empty execution IDs throw `std::invalid_argument`

## Future backend layout

Additional backends should be added under the same directory pattern:

```text
include/wf/backend/sqlite/
src/backend/sqlite/

include/wf/backend/postgres/
src/backend/postgres/

include/wf/backend/rocksdb/
src/backend/rocksdb/
```

The core service should continue depending on store interfaces, not backend-specific classes.

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
make clean        Remove build outputs and generated docs/*.png
make help         Show available targets
```

## Current development status

Implemented:

- custom JSON parser
- workflow definition model
- workflow definition parser and validator
- workflow execution model
- workflow service API surface
- workflow orchestrator API surface
- store interfaces
- memory backend store implementations
- parser tests
- memory backend store tests
- PlantUML architecture diagram
- PlantUML workflow execution sequence diagram
- Makefile build/test/format/docs workflow

Planned next work:

- add orchestrator unit tests
- finalize any missing orchestrator behavior
- add service unit tests
- add example workflow definition files
- add an executable sample or CLI driver
- add HTTP API layer
- add persistent backends such as SQLite, Postgres, or RocksDB
