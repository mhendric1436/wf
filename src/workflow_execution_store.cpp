#include "wf/store/workflow_execution_store.hpp"

namespace workflow
{
namespace
{

// WorkflowExecutionStore is currently an interface-only abstraction.
// Concrete implementations, such as InMemoryWorkflowExecutionStore or a persistent
// database-backed store, should live in their own source files.
[[maybe_unused]] constexpr int workflow_execution_store_translation_unit_anchor = 0;

} // namespace
} // namespace workflow
