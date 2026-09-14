#pragma once
#include "graphx/execution.hpp"
#include "graphx/config_value.hpp"
namespace graphx::infra::detail {
int execute_scenario(const ExecutionOptions&, const ConfigValue&, std::ostream&);
}
