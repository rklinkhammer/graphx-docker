#include "infra/command_runner.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using graphx::infra::detail::BackgroundOptions;
using graphx::infra::detail::CommandOptions;

void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

extern "C" void alarm_handler(int) {}

int fixture(int argc, char** argv) {
  const std::string mode = argv[2];
  if (mode == "arguments") {
    for (int index = 3; index < argc; ++index) std::cout << '[' << argv[index] << ']';
    return 0;
  }
  if (mode == "input") {
    std::string value((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::cout << value;
    return 0;
  }
  if (mode == "close-input") return 0;
  if (mode == "output") {
    std::cout << std::string(100, 'x');
    return 0;
  }
  if (mode == "exit") return std::atoi(argv[3]);
  if (mode == "signal") {
    std::raise(SIGTERM);
    return 1;
  }
  if (mode == "sleep") {
    ::usleep(static_cast<useconds_t>(std::atoi(argv[3])) * 1000U);
    return 0;
  }
  return 2;
}

void reap(pid_t pid) {
  ::kill(pid, SIGKILL);
  while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
  }
}

void tests(const std::string& executable) {
  using graphx::infra::detail::format_arguments;
  using graphx::infra::detail::inspect_process;
  using graphx::infra::detail::run_command;
  using graphx::infra::detail::spawn_background;

  const auto literal = run_command(
      {.arguments = {executable, "--fixture", "arguments", "a b", ";touch", "$(false)", "*.yaml"},
       .capture_output = true});
  expect(literal.status == 0 && literal.exec_error == 0 &&
             literal.output == "[a b][;touch][$(false)][*.yaml]",
         "argv values were not passed literally");

  const auto input = run_command({.arguments = {executable, "--fixture", "input"},
                                  .standard_input = "line one\nline two",
                                  .capture_output = true,
                                  .trim_trailing_newlines = false});
  expect(input.status == 0 && input.output == "line one\nline two", "stdin was not transferred");

  const auto closed_input = run_command({.arguments = {executable, "--fixture", "close-input"},
                                         .standard_input = std::string(1024 * 1024, 'x')});
  expect(closed_input.status == 0, "closed child stdin was not handled safely");

  const auto bounded = run_command({.arguments = {executable, "--fixture", "output"},
                                    .capture_output = true,
                                    .output_limit = 16});
  expect(bounded.status == 0 && bounded.output.size() == 16 && bounded.output_truncated,
         "captured output was not bounded");

  const auto missing = run_command({.arguments = {"graphx-s3-command-that-does-not-exist"}});
  const auto exited = run_command({.arguments = {executable, "--fixture", "exit", "127"}});
  expect(missing.status == 127 && missing.exec_error == ENOENT,
         "failed exec was not reported distinctly");
  expect(exited.status == 127 && exited.exec_error == 0,
         "ordinary command exit was mistaken for failed exec");

  const auto signalled = run_command({.arguments = {executable, "--fixture", "signal"}});
  expect(signalled.status == 128 + SIGTERM && signalled.termination_signal == SIGTERM,
         "signal termination was not reported");

  struct sigaction action{};
  action.sa_handler = alarm_handler;
  sigemptyset(&action.sa_mask);
  expect(::sigaction(SIGALRM, &action, nullptr) == 0, "cannot install alarm handler");
  ::ualarm(10'000, 0);
  const auto interrupted = run_command({.arguments = {executable, "--fixture", "sleep", "75"}});
  ::ualarm(0, 0);
  expect(interrupted.status == 0, "interrupted wait changed child result");
  int ignored{};
  errno = 0;
  expect(::waitpid(-1, &ignored, WNOHANG) == -1 && errno == ECHILD,
         "completed child was not reaped");

  const auto rendered = format_arguments({"ip", "route", "a b", "it's", "plain"});
  expect(rendered == "ip route 'a b' 'it'\\''s' plain", "dry-run formatting changed");

  const auto failed_background =
      spawn_background({.arguments = {"graphx-s3-background-that-does-not-exist"}});
  expect(failed_background.exec_error == ENOENT, "background exec failure was not reported");
  while (::waitpid(static_cast<pid_t>(failed_background.pid), nullptr, 0) < 0 && errno == EINTR) {
  }

  const auto background = spawn_background(
      BackgroundOptions{.arguments = {executable, "--fixture", "sleep", "2000", "s3-marker"}});
  expect(background.exec_error == 0 && background.pid != 0, "background process did not start");
#if defined(__linux__)
  bool identified{};
  for (int attempt = 0; attempt < 50; ++attempt) {
    const auto identity = inspect_process(background.pid);
    if (!identity.start_time.empty() && identity.command.find("s3-marker") != std::string::npos) {
      identified = true;
      break;
    }
    ::usleep(10'000);
  }
  expect(identified, "background process identity was not observable");
#endif
  reap(static_cast<pid_t>(background.pid));
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc >= 3 && std::string(argv[1]) == "--fixture") return fixture(argc, argv);
    tests(argv[0]);
    std::cout << "GraphX command runner tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
