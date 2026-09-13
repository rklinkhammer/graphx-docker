#pragma once
#include "graphx/execution.hpp"
#include "graphx/config_value.hpp"
namespace graphx::infra::detail {
int execute_capture_handoff(const ExecutionOptions& options, const ConfigValue& resolved,
                            std::ostream& output);
int execute_compose(const ExecutionOptions& options, const ConfigValue& resolved,
                    std::ostream& output);
}  // namespace graphx::infra::detail
