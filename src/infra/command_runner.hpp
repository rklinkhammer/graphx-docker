#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace graphx::infra::detail {

inline constexpr std::size_t default_output_limit = 64 * 1024;

struct CommandOptions {
  std::vector<std::string> arguments;
  std::string standard_input;
  bool capture_output{};
  std::size_t output_limit{default_output_limit};
  bool trim_trailing_newlines{true};
};

struct CommandResult {
  int status{};
  int termination_signal{};
  int exec_error{};
  std::string output;
  bool output_truncated{};
};

struct BackgroundOptions {
  std::vector<std::string> arguments;
  std::filesystem::path diagnostic_path;
};

struct BackgroundResult {
  std::uint32_t pid{};
  int exec_error{};
};

struct ProcessIdentity {
  std::string start_time;
  std::string command;
  std::string name;
};

// Executes an argv vector directly. Parent-side setup failures throw
// std::system_error; exec failures are reported separately from child exit status.
CommandResult run_command(const CommandOptions& options);

// Starts a detached process with standard input/output connected to /dev/null.
// An optional mode-0600 diagnostic file receives standard error.
BackgroundResult spawn_background(const BackgroundOptions& options);

ProcessIdentity inspect_process(std::uint32_t pid);

// This intentionally preserves the established GraphX dry-run representation.
std::string format_arguments(const std::vector<std::string>& arguments);

}  // namespace graphx::infra::detail
