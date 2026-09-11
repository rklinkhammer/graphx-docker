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
  std::vector<std::string> rollback_arguments;
  std::vector<std::string> rollback_identity_arguments;
};

[[nodiscard]] InfraCommand route_command(const GraphConfig& config, std::string_view router,
                                         std::string_view destination, bool clear);
[[nodiscard]] std::string format_command(const InfraCommand& command);
int execute_infrastructure_plan(const std::vector<InfraCommand>& commands, bool dry_run,
                                std::ostream& output, std::ostream& errors);

}  // namespace graphx
