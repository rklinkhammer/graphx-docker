#include "infra/command_runner.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

namespace graphx::infra::detail {
namespace {

class Descriptor {
 public:
  explicit Descriptor(int value = -1) noexcept : value_(value) {}
  ~Descriptor() {
    if (value_ >= 0) ::close(value_);
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&& other) noexcept : value_(other.release()) {}
  Descriptor& operator=(Descriptor&& other) noexcept {
    if (this != &other) {
      if (value_ >= 0) ::close(value_);
      value_ = other.release();
    }
    return *this;
  }
  [[nodiscard]] int get() const noexcept { return value_; }
  [[nodiscard]] int release() noexcept {
    const auto value = value_;
    value_ = -1;
    return value;
  }
  void reset() noexcept {
    if (value_ >= 0) ::close(value_);
    value_ = -1;
  }

 private:
  int value_;
};

class ChildGuard {
 public:
  explicit ChildGuard(pid_t pid) noexcept : pid_(pid) {}
  ~ChildGuard() {
    if (pid_ <= 0) return;
    ::kill(pid_, SIGKILL);
    while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
  }
  ChildGuard(const ChildGuard&) = delete;
  ChildGuard& operator=(const ChildGuard&) = delete;
  void release() noexcept { pid_ = -1; }

 private:
  pid_t pid_;
};

[[noreturn]] void throw_errno(std::string_view operation) {
  throw std::system_error(errno, std::generic_category(), std::string(operation));
}

class SigpipeBlock {
 public:
  explicit SigpipeBlock(bool enabled) : enabled_(enabled) {
    if (!enabled_) return;
    sigset_t pending{};
    sigemptyset(&signal_set_);
    sigaddset(&signal_set_, SIGPIPE);
    if (::sigpending(&pending) != 0) throw_errno("sigpending");
    was_pending_ = sigismember(&pending, SIGPIPE) == 1;
    const auto status = ::pthread_sigmask(SIG_BLOCK, &signal_set_, &old_mask_);
    if (status != 0) throw std::system_error(status, std::generic_category(), "pthread_sigmask");
  }
  ~SigpipeBlock() {
    if (!enabled_) return;
    sigset_t pending{};
    if (!was_pending_ && ::sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
      int signal{};
      (void)::sigwait(&signal_set_, &signal);
    }
    (void)::pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr);
  }
  SigpipeBlock(const SigpipeBlock&) = delete;
  SigpipeBlock& operator=(const SigpipeBlock&) = delete;

 private:
  bool enabled_{};
  bool was_pending_{};
  sigset_t signal_set_{};
  sigset_t old_mask_{};
};

void make_pipe(int descriptors[2]) {
  if (::pipe(descriptors) != 0) throw_errno("pipe");
}

void close_descriptor(int descriptor) {
  if (descriptor >= 0) ::close(descriptor);
}

void set_close_on_exec(int descriptor) {
  const auto flags = ::fcntl(descriptor, F_GETFD);
  if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0)
    throw_errno("fcntl FD_CLOEXEC");
}

void child_report_exec_error(int descriptor, int value) noexcept {
  const auto ignored = ::write(descriptor, &value, sizeof(value));
  (void)ignored;
}

std::vector<char*> argv_for(const std::vector<std::string>& arguments) {
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
  argv.push_back(nullptr);
  return argv;
}

int wait_for(pid_t child, int options = 0) {
  int status{};
  for (;;) {
    const auto result = ::waitpid(child, &status, options);
    if (result == child) return status;
    if (result == 0) return -1;
    if (errno != EINTR) throw_errno("waitpid");
  }
}

int read_exec_error(int descriptor) {
  int value{};
  auto offset = std::size_t{};
  while (offset < sizeof(value)) {
    const auto count =
        ::read(descriptor, reinterpret_cast<char*>(&value) + offset, sizeof(value) - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) throw_errno("read exec status");
    break;
  }
  return offset == sizeof(value) ? value : 0;
}

void transfer_io(Descriptor& input, std::string_view contents, Descriptor& output,
                 CommandResult& result, std::size_t limit) {
  std::size_t input_offset{};
  std::array<char, 4096> buffer{};
  while (input.get() >= 0 || output.get() >= 0) {
    std::array<pollfd, 2> descriptors{{
        {input.get(), static_cast<short>(input.get() >= 0 ? POLLOUT : 0), 0},
        {output.get(), static_cast<short>(output.get() >= 0 ? POLLIN : 0), 0},
    }};
    int ready{};
    do {
      ready = ::poll(descriptors.data(), descriptors.size(), -1);
    } while (ready < 0 && errno == EINTR);
    if (ready < 0) throw_errno("poll");

    if (input.get() >= 0 && (descriptors[0].revents & (POLLOUT | POLLHUP | POLLERR))) {
      const auto count =
          ::write(input.get(), contents.data() + input_offset, contents.size() - input_offset);
      if (count > 0) input_offset += static_cast<std::size_t>(count);
      if (count < 0 && errno != EINTR && errno != EPIPE) throw_errno("write child input");
      if (count <= 0 || input_offset == contents.size()) input.reset();
    }

    if (output.get() >= 0 && (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR))) {
      const auto count = ::read(output.get(), buffer.data(), buffer.size());
      if (count > 0) {
        const auto available = limit > result.output.size() ? limit - result.output.size() : 0;
        const auto keep = std::min(available, static_cast<std::size_t>(count));
        result.output.append(buffer.data(), keep);
        result.output_truncated |= keep != static_cast<std::size_t>(count);
      } else if (count == 0) {
        output.reset();
      } else if (errno != EINTR) {
        throw_errno("read child output");
      }
    }
  }
}

void detach_child_io(int exec_status, const std::filesystem::path& diagnostic_path) noexcept {
  (void)::setsid();
  const auto null_descriptor = ::open("/dev/null", O_RDWR);
  if (null_descriptor < 0 || ::dup2(null_descriptor, STDIN_FILENO) < 0 ||
      ::dup2(null_descriptor, STDOUT_FILENO) < 0) {
    child_report_exec_error(exec_status, errno == 0 ? EIO : errno);
    ::_exit(126);
  }
  if (diagnostic_path.empty()) {
    if (::dup2(null_descriptor, STDERR_FILENO) < 0) {
      child_report_exec_error(exec_status, errno);
      ::_exit(126);
    }
  } else {
    const auto diagnostic =
        ::open(diagnostic_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (diagnostic < 0 || ::dup2(diagnostic, STDERR_FILENO) < 0) {
      child_report_exec_error(exec_status, errno == 0 ? EIO : errno);
      ::_exit(126);
    }
    if (diagnostic > STDERR_FILENO) ::close(diagnostic);
  }
  if (null_descriptor > STDERR_FILENO) ::close(null_descriptor);

  const auto maximum = ::sysconf(_SC_OPEN_MAX);
  for (int descriptor = 3; descriptor < (maximum > 0 ? maximum : 1024); ++descriptor)
    if (descriptor != exec_status) ::close(descriptor);
}

}  // namespace

CommandResult run_command(const CommandOptions& options) {
  if (options.arguments.empty()) throw std::invalid_argument("command argv must not be empty");

  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  int exec_pipe[2] = {-1, -1};
  if (!options.standard_input.empty()) make_pipe(input_pipe);
  try {
    if (options.capture_output) make_pipe(output_pipe);
    make_pipe(exec_pipe);
    set_close_on_exec(exec_pipe[1]);
  } catch (...) {
    close_descriptor(input_pipe[0]);
    close_descriptor(input_pipe[1]);
    close_descriptor(output_pipe[0]);
    close_descriptor(output_pipe[1]);
    close_descriptor(exec_pipe[0]);
    close_descriptor(exec_pipe[1]);
    throw;
  }

  const auto child = ::fork();
  if (child < 0) {
    const auto saved_errno = errno;
    close_descriptor(input_pipe[0]);
    close_descriptor(input_pipe[1]);
    close_descriptor(output_pipe[0]);
    close_descriptor(output_pipe[1]);
    close_descriptor(exec_pipe[0]);
    close_descriptor(exec_pipe[1]);
    errno = saved_errno;
    throw_errno("fork");
  }
  if (child == 0) {
    close_descriptor(exec_pipe[0]);
    if (input_pipe[0] >= 0) {
      close_descriptor(input_pipe[1]);
      if (::dup2(input_pipe[0], STDIN_FILENO) < 0) {
        child_report_exec_error(exec_pipe[1], errno);
        ::_exit(126);
      }
      close_descriptor(input_pipe[0]);
    }
    if (output_pipe[1] >= 0) {
      close_descriptor(output_pipe[0]);
      if (::dup2(output_pipe[1], STDOUT_FILENO) < 0) {
        child_report_exec_error(exec_pipe[1], errno);
        ::_exit(126);
      }
      close_descriptor(output_pipe[1]);
    }
    auto argv = argv_for(options.arguments);
    ::execvp(argv.front(), argv.data());
    const auto exec_error = errno;
    child_report_exec_error(exec_pipe[1], exec_error);
    ::_exit(exec_error == ENOENT ? 127 : 126);
  }

  ChildGuard guard(child);
  close_descriptor(input_pipe[0]);
  close_descriptor(output_pipe[1]);
  close_descriptor(exec_pipe[1]);
  Descriptor input(input_pipe[1]);
  Descriptor output(output_pipe[0]);
  Descriptor exec_status(exec_pipe[0]);
  CommandResult result;
  {
    SigpipeBlock blocked_sigpipe(input.get() >= 0);
    transfer_io(input, options.standard_input, output, result, options.output_limit);
  }
  const auto wait_status = wait_for(child);
  guard.release();
  result.exec_error = read_exec_error(exec_status.get());
  if (options.trim_trailing_newlines)
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r'))
      result.output.pop_back();
  if (WIFEXITED(wait_status)) {
    result.status = WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    result.termination_signal = WTERMSIG(wait_status);
    result.status = 128 + result.termination_signal;
  } else {
    result.status = 1;
  }
  return result;
}

BackgroundResult spawn_background(const BackgroundOptions& options) {
  if (options.arguments.empty()) throw std::invalid_argument("command argv must not be empty");
  int exec_pipe[2] = {-1, -1};
  make_pipe(exec_pipe);
  try {
    set_close_on_exec(exec_pipe[1]);
  } catch (...) {
    close_descriptor(exec_pipe[0]);
    close_descriptor(exec_pipe[1]);
    throw;
  }
  const auto child = ::fork();
  if (child < 0) {
    const auto saved_errno = errno;
    close_descriptor(exec_pipe[0]);
    close_descriptor(exec_pipe[1]);
    errno = saved_errno;
    throw_errno("fork");
  }
  if (child == 0) {
    close_descriptor(exec_pipe[0]);
    detach_child_io(exec_pipe[1], options.diagnostic_path);
    auto argv = argv_for(options.arguments);
    ::execvp(argv.front(), argv.data());
    const auto exec_error = errno;
    child_report_exec_error(exec_pipe[1], exec_error);
    ::_exit(exec_error == ENOENT ? 127 : 126);
  }
  close_descriptor(exec_pipe[1]);
  Descriptor exec_status(exec_pipe[0]);
  return {static_cast<std::uint32_t>(child), read_exec_error(exec_status.get())};
}

ProcessIdentity inspect_process(std::uint32_t pid) {
  ProcessIdentity identity;
#if defined(__linux__)
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string value;
  std::getline(stat, value);
  const auto close = value.rfind(')');
  if (close != std::string::npos && close + 2 < value.size()) {
    std::istringstream fields(value.substr(close + 2));
    std::string field;
    for (int index = 0; index <= 19; ++index) {
      if (!(fields >> field)) {
        field.clear();
        break;
      }
    }
    identity.start_time = std::move(field);
  }
  std::ifstream command("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
  identity.command.assign(std::istreambuf_iterator<char>(command),
                          std::istreambuf_iterator<char>());
  std::replace(identity.command.begin(), identity.command.end(), '\0', ' ');
  std::ifstream name("/proc/" + std::to_string(pid) + "/comm");
  std::getline(name, identity.name);
#else
  (void)pid;
#endif
  return identity;
}

std::string format_arguments(const std::vector<std::string>& arguments) {
  std::string result;
  for (const auto& argument : arguments) {
    if (!result.empty()) result += ' ';
    const bool quote = argument.find_first_of(" \t'\"") != std::string::npos;
    if (!quote) {
      result += argument;
      continue;
    }
    result += '\'';
    for (const char value : argument) result += value == '\'' ? "'\\''" : std::string(1, value);
    result += '\'';
  }
  return result;
}

}  // namespace graphx::infra::detail
