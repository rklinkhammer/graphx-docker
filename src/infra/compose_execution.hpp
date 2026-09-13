#pragma once
#include "graphx/execution.hpp"
#include "graphx/config_value.hpp"
namespace graphx::infra::detail {
int execute_compose(const ExecutionOptions& options, const ConfigValue& resolved,
                    std::ostream& output);
}
