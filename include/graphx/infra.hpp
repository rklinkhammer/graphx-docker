#pragma once

#include "graphx/config.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace graphx {

struct InfraCommand {
  std::vector<std::string> arguments;
  std::string standard_input;
  bool ignore_failure{};
};

enum class InfraAction { create, destroy, status };

[[nodiscard]] std::vector<InfraCommand> infrastructure_plan(const GraphConfig& config,
                                                            InfraAction action);
[[nodiscard]] InfraCommand netem_command(const GraphConfig& config, std::string_view router,
                                         std::string_view interface, bool clear,
                                         std::string delay = {}, std::string jitter = {},
                                         std::string loss = {}, std::string rate = {});
[[nodiscard]] std::string format_command(const InfraCommand& command);
int execute_infrastructure_plan(const std::vector<InfraCommand>& commands, bool dry_run,
                                std::ostream& output, std::ostream& errors);

}  // namespace graphx
