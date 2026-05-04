# Agent Guidelines

## Project Shape

This repository is a lightweight C++20 workflow definition parser, validator, and workflow
service framework.

- `include/wf/` contains the public API headers.
- `src/` contains the implementation files that correspond to public headers.
- `include/wf/json.hpp` and `src/json.cpp` implement the built-in JSON value type and parser.
- `include/wf/workflow_definition.hpp` contains passive workflow definition model types.
- `include/wf/workflow_execution.hpp` and `include/wf/workflow_step_execution.hpp` contain workflow
  and step execution model types.
- `include/wf/workflow_parser.hpp` and `src/workflow_parser.cpp` parse and validate workflow
  definition JSON.
- `include/wf/workflow_logic.hpp` defines application-owned transition logic.
- `include/wf/workflow_orchestrator.hpp` and `src/workflow_orchestrator.cpp` coordinate workflow
  execution through store interfaces and workflow logic.
- `include/wf/workflow_service.hpp` and `src/workflow_service.cpp` provide the service facade.
- `include/wf/store/` contains backend-neutral persistence interfaces.
- `include/wf/backend/memory/` and `src/backend/memory/` contain the process-local in-memory
  backend implementations.
- `tests/` contains Catch2 unit tests.
- `third_party/catch2/` contains the vendored Catch2 amalgamated files.
- `docs/` contains PlantUML architecture and sequence diagrams.

## Build And Test

Use the existing Makefile targets:

```sh
make
```

This builds and runs the full test binary.

Useful targeted commands:

```sh
make build
make test
make format-check
make format
make docs-png
make clean
```

`make docs-png` requires PlantUML, and may require Graphviz depending on the local PlantUML
installation.

## Working Rules

- Do not make code changes when the request is analysis-only.
- Prefer small, focused changes that preserve the dependency-minimal design.
- Do not introduce external runtime dependencies unless explicitly requested.
- Preserve C++20 compatibility.
- Use `rg` or `rg --files` for search.
- Run `make test` after code changes when feasible.
- Run `make format-check` or `make format` when touching C++ files.
- Keep Catch2 as the only vendored third-party code unless the user explicitly asks otherwise.
- Do not edit generated diagram PNGs directly; update `docs/*.puml` and regenerate with
  `make docs-png`.

## Style

- Follow `.clang-format`; the project uses LLVM-derived formatting with 4-space indentation and
  Allman braces.
- Public declarations live in headers under `include/wf/`; implementation lives in matching
  `src/` paths.
- Use the existing `workflow` namespace, and `workflow::json` for JSON parser/value code.
- Keep model structs simple and value-oriented unless the surrounding code already uses an
  abstraction.
- Prefer standard library types already used in the project: `std::string`, `std::optional`,
  `std::vector`, `std::map`, and references for injected stores/services.

## Workflow Definition Rules

The parser/validator currently treats workflow definitions as externally driven step lists.
Preserve these semantics unless the task explicitly changes them:

- Top-level workflow definitions must include `workflowName`, `workflowVersion`,
  `startWorkflowStepName`, `expectedExecutionTime`, and non-empty `steps`.
- `workflowName` must start with a letter and contain only letters, digits, `_`, or `-`.
- `workflowVersion` must be an integer greater than or equal to `1`.
- Duration fields use ISO-8601 strings such as `PT30S`, `PT5M`, or `PT1H`.
- Step names must be unique, and `startWorkflowStepName` must match a declared step.
- Step `maxRetries`, when present, must be a non-negative integer.
- Additional top-level fields are rejected.
- Additional step-level fields are preserved in `WorkflowStep::additionalFields`.
- Do not add workflow transition behavior to definition fields such as `nextStep`, `service`, or
  `type`; transition decisions belong in `WorkflowLogic`.

## JSON Parser Notes

- The project intentionally uses its own JSON parser instead of `nlohmann/json`.
- `workflow::json::Value` supports objects, arrays, strings, integers, floating-point numbers,
  booleans, and null.
- Unicode escape sequences such as `\uXXXX` are intentionally rejected for now. Do not silently
  decode or accept them without updating tests and documentation.
- Prefer structured access through `Value` APIs over ad hoc string parsing.

## Backend Boundaries

- Core service and orchestration code should depend on interfaces in `include/wf/store/`, not on
  backend-specific classes.
- Memory backend code belongs under `include/wf/backend/memory/`, `src/backend/memory/`, and
  `tests/backend/memory/`.
- Future persistent backends should follow the same layout pattern, for example
  `include/wf/backend/sqlite/`, `src/backend/sqlite/`, and `tests/backend/sqlite/`.
- Keep in-memory stores process-local and non-durable; do not imply persistence across process
  restarts.

## Test Guidance

- Add focused Catch2 tests near the behavior being changed.
- Parser and validation behavior belongs in `tests/workflow_parser_tests.cpp` unless it grows
  enough to justify splitting.
- Memory backend behavior belongs under `tests/backend/memory/`.
- When changing orchestrator or service behavior, add dedicated tests rather than only testing via
  lower-level stores.
- Prefer assertions that verify observable behavior and error cases, especially validation errors,
  retry handling, store replacement/update semantics, and worker claim behavior.

## Current Design Notes

- Workflow transition logic is application-owned through `WorkflowLogic`.
- `WorkflowService` is a thin facade over parser validation and `WorkflowOrchestrator`.
- `WorkflowOrchestrator` creates workflow executions at the definition's declared start step and
  advances or completes executions based on workflow logic decisions.
- Failed steps use the step definition's `maxRetries` for retry behavior.
- In-memory definition saves replace existing definitions with the same name/version.
- In-memory execution saves replace existing executions with the same execution ID.
- In-memory execution updates require an existing execution.
- Invalid definition keys and empty execution IDs throw `std::invalid_argument`.

## Suggested Fix Priority

1. Add orchestrator unit tests around start, poll/claim, complete, fail, and retry behavior.
2. Add service unit tests for validation, registration, and request forwarding.
3. Add example workflow definition files and an executable sample or CLI driver.
