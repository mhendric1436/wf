# Workflow Definition Parser

A small C++20 project for parsing and validating workflow definition JSON files.

The project includes:

- A custom recursive-descent JSON parser
- A workflow definition model
- A workflow validator
- Unit tests using Catch2
- A regular `Makefile` build using `clang++`
- Code formatting with `clang-format`

No general-purpose JSON library is required.

## Workflow Definition Model

A workflow definition is uniquely identified by:

- `workflowName`
- `workflowVersion`
- `startWorkflowStepName`

A workflow also contains:

- `expectedExecutionTime` for the overall workflow
- `steps`, a list of workflow steps

Each workflow step contains:

- `name`
- `expectedExecutionTime`
- `maxRetries`

Each step name must be unique within a workflow definition.

The `startWorkflowStepName` must match the name of one of the workflow steps.

Workflow transition logic is intentionally not modeled in the workflow definition. The workflow business logic is expected to determine and request the next step to execute at runtime.

## Example Workflow JSON

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

## Repository Layout

```text
workflow-definition/
├── Makefile
├── .clang-format
├── .gitignore
├── include/
│   └── workflow/
│       ├── custom_json.hpp
│       ├── workflow_definition.hpp
│       └── workflow_parser.hpp
├── src/
│   ├── custom_json.cpp
│   └── workflow_parser.cpp
├── tests/
│   └── workflow_parser_tests.cpp
└── third_party/
    └── catch2/
        ├── catch_amalgamated.hpp
        └── catch_amalgamated.cpp
```

## Requirements

- C++20 compiler
- `clang++`
- `clang-format`
- `make`
- Catch2 amalgamated test files

On macOS:

```bash
brew install llvm clang-format
```

Depending on your shell environment, Homebrew LLVM may not be first on your `PATH`. You can override the compiler when running `make`:

```bash
make CXX=/opt/homebrew/opt/llvm/bin/clang++
```

For Intel macOS Homebrew installations, the path may be:

```bash
make CXX=/usr/local/opt/llvm/bin/clang++
```

## Download Catch2

This project expects Catch2 amalgamated files under `third_party/catch2`.

```bash
mkdir -p third_party/catch2

curl -L \
  https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.hpp \
  -o third_party/catch2/catch_amalgamated.hpp

curl -L \
  https://github.com/catchorg/Catch2/releases/download/v3.5.4/catch_amalgamated.cpp \
  -o third_party/catch2/catch_amalgamated.cpp
```

## Build and Test

```bash
make
```

Equivalent explicit command:

```bash
make test
```

The test binary is written to:

```text
build/bin/workflow_parser_tests
```

## Format Code

Format all project source files:

```bash
make format
```

Check formatting without modifying files:

```bash
make format-check
```

## Clean Build Outputs

```bash
make clean
```

## Makefile Targets

```text
make              Build and run tests
make test         Build and run tests
make format       Format source files with clang-format
make format-check Check formatting without modifying files
make clean        Remove build outputs
make help         Show available targets
```

## Validation Rules

The validator checks the following top-level rules:

- The root JSON value must be an object.
- `workflowName` is required.
- `workflowName` must be a non-empty string.
- `workflowName` must match `^[A-Za-z][A-Za-z0-9_-]*$`.
- `workflowVersion` is required.
- `workflowVersion` must be an integer greater than or equal to `1`.
- `startWorkflowStepName` is required.
- `startWorkflowStepName` must be a non-empty string.
- `expectedExecutionTime` is required.
- `expectedExecutionTime` must be an ISO-8601 duration string.
- `steps` is required.
- `steps` must be a non-empty array.
- Additional top-level fields are rejected.

The validator checks the following step-level rules:

- Each step must be an object.
- `name` is required.
- `name` must be a non-empty string.
- `name` must match `^[A-Za-z][A-Za-z0-9_-]*$`.
- Step names must be unique within the workflow definition.
- `expectedExecutionTime`, when present, must be an ISO-8601 duration string.
- `maxRetries`, when present, must be an integer greater than or equal to `0`.

The validator also checks:

- `startWorkflowStepName` must match one of the step names.

## Custom JSON Parser

The custom parser supports:

- JSON objects
- JSON arrays
- Strings
- Integers
- Floating-point numbers
- Booleans
- `null`
- Standard string escapes such as `\\`, `\"`, `\n`, `\r`, and `\t`

The parser intentionally does not decode `\uXXXX` Unicode escape sequences yet. If a Unicode escape sequence is encountered, the parser reports a parse error rather than silently producing incorrect data.

## Core API

### Parse JSON Text

```cpp
workflow::WorkflowDefinition workflow =
    workflow::parseWorkflowDefinitionText(jsonText);
```

### Validate an Already Parsed JSON Value

```cpp
auto value = workflow::json::parse(jsonText);
auto result = workflow::validateWorkflowJson(value);

if (!result.valid) {
    for (const auto& error : result.errors) {
        // handle validation error
    }
}
```

### Parse an Already Parsed JSON Value

```cpp
auto value = workflow::json::parse(jsonText);
workflow::WorkflowDefinition workflow = workflow::parseWorkflowDefinition(value);
```

`parseWorkflowDefinition` throws `std::invalid_argument` if validation fails.

`parseWorkflowDefinitionText` throws `std::invalid_argument` if the JSON text is invalid or the workflow definition fails validation.

## Notes

This project deliberately keeps workflow transitions out of the static workflow definition. Fields such as `nextStep`, `service`, and `type` are not part of the core step model.

The workflow business logic is responsible for deciding the next step to execute.

## Suggested Next Enhancements

Potential future improvements include:

- Add file-based workflow loading helpers
- Add JSON serialization for workflow definitions
- Add `\uXXXX` Unicode escape decoding
- Add a command-line validator executable
- Add CI using `make format-check` and `make test`
