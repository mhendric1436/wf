#include "wf/store/workflow_definition_store.hpp"

namespace workflow
{
namespace
{

// WorkflowDefinitionStore is currently an interface-only abstraction.
// Concrete implementations, such as InMemoryWorkflowDefinitionStore or a persistent
// database-backed store, should live in their own source files.
[[maybe_unused]] constexpr int workflow_definition_store_translation_unit_anchor = 0;

} // namespace
} // namespace workflow
