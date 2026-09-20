#pragma once
#include "graphx/execution.hpp"
#include "graphx/config_value.hpp"
namespace graphx::infra::detail {
void validate_compose_service_fields(const std::string& name, const ConfigValue& service,
                                     const ConfigValue& resolved);
int execute_capture_handoff(const ExecutionOptions& options, const ConfigValue& resolved,
                            std::ostream& output);
int execute_compose(const ExecutionOptions& options, const ConfigValue& resolved,
                    std::ostream& output);
}  // namespace graphx::infra::detail
