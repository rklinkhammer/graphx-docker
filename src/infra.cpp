#include "graphx/infra.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace graphx {
namespace {

int execute_command(const InfraCommand& command_value, std::ostream& errors) {
  int input_pipe[2] = {-1, -1};
  if (!command_value.standard_input.empty() && ::pipe(input_pipe) != 0) {
    errors << "graphx: pipe: " << std::strerror(errno) << '\n';
    return 1;
  }
  const auto child = ::fork();
  if (child < 0) {
    errors << "graphx: fork: " << std::strerror(errno) << '\n';
    return 1;
  }
  if (child == 0) {
    if (input_pipe[0] >= 0) {
      ::close(input_pipe[1]);
      ::dup2(input_pipe[0], STDIN_FILENO);
      ::close(input_pipe[0]);
    }
    std::vector<char*> argv;
    argv.reserve(command_value.arguments.size() + 1);
    for (const auto& argument : command_value.arguments)
      argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }
  if (input_pipe[0] >= 0) {
    ::close(input_pipe[0]);
    const char* data = command_value.standard_input.data();
    std::size_t remaining = command_value.standard_input.size();
    while (remaining > 0) {
      const auto written = ::write(input_pipe[1], data, remaining);
      if (written <= 0) break;
      data += written;
      remaining -= static_cast<std::size_t>(written);
    }
    ::close(input_pipe[1]);
  }
  int status{};
  if (::waitpid(child, &status, 0) < 0) return 1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

int capture_command(const std::vector<std::string>& arguments, std::string& output,
                    std::ostream& errors) {
  int output_pipe[2];
  if (::pipe(output_pipe) != 0) {
    errors << "graphx: pipe: " << std::strerror(errno) << '\n';
    return 1;
  }
  const auto child = ::fork();
  if (child < 0) {
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    errors << "graphx: fork: " << std::strerror(errno) << '\n';
    return 1;
  }
  if (child == 0) {
    ::close(output_pipe[0]);
    ::dup2(output_pipe[1], STDOUT_FILENO);
    ::close(output_pipe[1]);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }
  ::close(output_pipe[1]);
  output.clear();
  char buffer[256];
  for (;;) {
    const auto count = ::read(output_pipe[0], buffer, sizeof(buffer));
    if (count > 0) {
      output.append(buffer, static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    break;
  }
  ::close(output_pipe[0]);
  int status{};
  if (::waitpid(child, &status, 0) < 0) return 1;
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
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
  std::string result;
  for (const auto& argument : command_value.arguments) {
    if (!result.empty()) result += ' ';
    const bool quote = argument.find_first_of(" \t'\"") != std::string::npos;
    if (!quote)
      result += argument;
    else {
      result += '\'';
      for (const char value : argument) result += value == '\'' ? "'\\''" : std::string(1, value);
      result += '\'';
    }
  }
  return result;
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
