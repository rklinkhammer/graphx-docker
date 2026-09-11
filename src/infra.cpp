#include "graphx/infra.hpp"

#include "infra/command_runner.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace graphx {
namespace {

int execute_command(const InfraCommand& command_value, std::ostream& errors) {
  try {
    const auto result = infra::detail::run_command(
        {.arguments = command_value.arguments, .standard_input = command_value.standard_input});
    if (result.exec_error != 0)
      errors << "graphx: exec: " << std::strerror(result.exec_error) << '\n';
    return result.status;
  } catch (const std::system_error& error) {
    errors << "graphx: " << error.what() << '\n';
    return 1;
  }
}

int capture_command(const std::vector<std::string>& arguments, std::string& output,
                    std::ostream& errors) {
  try {
    infra::detail::CommandOptions options;
    options.arguments = arguments;
    options.capture_output = true;
    auto result = infra::detail::run_command(options);
    output = std::move(result.output);
    if (result.output_truncated) {
      errors << "graphx: command output exceeded " << infra::detail::default_output_limit
             << " bytes\n";
      return 1;
    }
    if (result.exec_error != 0)
      errors << "graphx: exec: " << std::strerror(result.exec_error) << '\n';
    return result.status;
  } catch (const std::system_error& error) {
    errors << "graphx: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace

InfraCommand route_command(const GraphConfig& config, std::string_view router_id,
                           std::string_view destination, bool clear) {
  const auto& router = config.network_infrastructure.router(router_id);
  if (router.kind != RouterKind::linux_namespace)
    throw std::invalid_argument("manual route realization requires a Linux namespace router");
  const auto found = std::ranges::find_if(
      router.routes, [&](const auto& value) { return value.destination == destination; });
  if (found == router.routes.end()) throw std::invalid_argument("unknown declared route");
  InfraCommand result;
  result.arguments = {"ip",
                      "netns",
                      "exec",
                      router.namespace_name,
                      "ip",
                      "route",
                      clear ? "delete" : "replace",
                      found->destination};
  if (!clear) {
    if (!found->via.empty()) result.arguments.insert(result.arguments.end(), {"via", found->via});
    if (!found->device.empty())
      result.arguments.insert(result.arguments.end(), {"dev", found->device});
  } else {
    result.ignore_failure = true;
  }
  return result;
}

std::string format_command(const InfraCommand& command_value) {
  return infra::detail::format_arguments(command_value.arguments);
}

int execute_infrastructure_plan(const std::vector<InfraCommand>& commands, bool dry_run,
                                std::ostream& output, std::ostream& errors) {
  struct CompletedCommand {
    const InfraCommand* command{};
    std::string identity;
  };
  std::vector<CompletedCommand> completed;
  const auto rollback_completed = [&] {
    for (auto iterator = completed.rbegin(); iterator != completed.rend(); ++iterator) {
      if (!iterator->command->rollback_identity_arguments.empty()) {
        std::string current_identity;
        const auto identity_status = capture_command(iterator->command->rollback_identity_arguments,
                                                     current_identity, errors);
        if (identity_status != 0 || current_identity != iterator->identity) {
          output << "! rollback skipped; resource identity changed: "
                 << format_command(*iterator->command) << '\n';
          continue;
        }
      }
      InfraCommand rollback;
      rollback.arguments = iterator->command->rollback_arguments;
      output << "- " << format_command(rollback) << '\n';
      const auto rollback_status = execute_command(rollback, errors);
      if (rollback_status != 0)
        errors << "graphx: rollback command failed with status " << rollback_status << ": "
               << format_command(rollback) << '\n';
    }
  };
  for (const auto& command_value : commands) {
    output << "+ " << format_command(command_value) << '\n';
    if (dry_run) continue;
    const auto status = execute_command(command_value, errors);
    if (status != 0 && !command_value.ignore_failure) {
      errors << "graphx: command failed with status " << status << ": "
             << format_command(command_value) << '\n';
      rollback_completed();
      return status;
    }
    if (status == 0 && !command_value.rollback_arguments.empty()) {
      std::string identity;
      if (!command_value.rollback_identity_arguments.empty()) {
        const auto identity_status =
            capture_command(command_value.rollback_identity_arguments, identity, errors);
        if (identity_status != 0) {
          errors << "graphx: failed to identify created resource: " << format_command(command_value)
                 << '\n';
          InfraCommand rollback;
          rollback.arguments = command_value.rollback_arguments;
          output << "- " << format_command(rollback) << '\n';
          const auto rollback_status = execute_command(rollback, errors);
          if (rollback_status != 0)
            errors << "graphx: rollback command failed with status " << rollback_status << ": "
                   << format_command(rollback) << '\n';
          rollback_completed();
          return identity_status;
        }
      }
      completed.push_back({&command_value, std::move(identity)});
    }
  }
  return 0;
}

}  // namespace graphx
