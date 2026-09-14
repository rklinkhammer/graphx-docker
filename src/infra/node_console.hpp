#pragma once
#include "graphx/execution.hpp"
#include "infra/ownership_state.hpp"
namespace graphx::infra::detail {
void prepare_node_console(const std::filesystem::path&, OwnershipState&);
void start_node_console(const ExecutionOptions&, OwnershipState&);
int execute_node_console(const ExecutionOptions&, const ConfigValue&);
}  // namespace graphx::infra::detail
