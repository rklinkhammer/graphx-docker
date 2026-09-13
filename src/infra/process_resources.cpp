#include "infra/process_resources.hpp"
#include <array>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#if defined(__linux__)
#include <sched.h>
#include <sys/prctl.h>
#endif

namespace graphx::infra::detail {
void require_available_tcp_port(std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) throw std::runtime_error("E_LISTENER: cannot inspect console port");
  const int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port);
  endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const auto result = ::bind(fd, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint));
  ::close(fd);
  if (result != 0) throw std::runtime_error("E_LISTENER_BUSY: console port is occupied");
}

void publish_execution_file(const std::filesystem::path& path, const std::string& bytes,
                            unsigned mode) {
  safe_execution_path(path, true);
  const auto temp = std::filesystem::path(path.string() + ".pending");
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
  if (fd < 0) throw std::runtime_error("E_EXECUTION_FILE: exclusive publication failed");
  const auto count = ::write(fd, bytes.data(), bytes.size());
  const bool success = count == static_cast<ssize_t>(bytes.size()) && ::fsync(fd) == 0;
  ::close(fd);
  if (!success || ::rename(temp.c_str(), path.c_str()) != 0)
    throw std::runtime_error("E_EXECUTION_FILE: publication failed; retained pending file");
  const int directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (directory < 0)
    throw std::runtime_error("E_EXECUTION_FILE: cannot open publication directory");
  const int synced = ::fsync(directory);
  ::close(directory);
  if (synced != 0) throw std::runtime_error("E_EXECUTION_FILE: cannot sync publication directory");
}
void safe_execution_path(const std::filesystem::path& path, bool absent) {
  if (!path.is_absolute() || path.lexically_normal() != path)
    throw std::runtime_error("E_EXECUTION_PATH: canonical absolute path required");
  std::filesystem::path current;
  for (const auto& component : path) {
    current /= component;
    struct stat metadata{};
    if (::lstat(current.c_str(), &metadata) != 0) {
      if (absent && errno == ENOENT) continue;
      throw std::runtime_error("E_EXECUTION_PATH: missing path " + current.string());
    }
    if (S_ISLNK(metadata.st_mode) || (!S_ISDIR(metadata.st_mode) && !S_ISREG(metadata.st_mode)) ||
        (S_ISREG(metadata.st_mode) && metadata.st_nlink != 1))
      throw std::runtime_error("E_EXECUTION_PATH: unsafe path " + current.string());
  }
}
namespace {
pid_t process_id(const OwnedResourceIdentity& resource) {
  unsigned int pid{};
  const auto& value = resource.stable_id;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), pid);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || pid < 2 ||
      pid > 2147483647)
    throw std::runtime_error("E_PROCESS_IDENTITY: invalid process ID");
  return static_cast<pid_t>(pid);
}
}  // namespace
int native_process_status(const OwnedResourceIdentity& resource) {
  const auto pid = process_id(resource);
  // Reap children from a failed startup; later invocations observe OS absence.
  if (::waitpid(pid, nullptr, WNOHANG) == pid) return 0;
  const auto observed = inspect_process(static_cast<std::uint32_t>(pid));
  if (observed.exited) return 0;
  if (observed.start_time.empty()) {
    if (::kill(pid, 0) == 0 || errno != ESRCH)
      throw std::runtime_error("E_PROCESS_IDENTITY: process identity unavailable");
    return 0;
  }
  const auto split = resource.process_identity.rfind('\n');
  if (split == std::string::npos || observed.start_time != resource.secondary_id ||
      observed.executable != resource.process_identity.substr(0, split))
    throw std::runtime_error("E_PROCESS_IDENTITY: process substitution; nothing signalled");
  const auto executable = resource.process_identity.substr(0, split);
  safe_execution_path(executable);
  if (configuration_hash(executable) != resource.process_identity.substr(split + 1))
    throw std::runtime_error("E_PROCESS_IDENTITY: executable changed; nothing signalled");
  return 1;
}
void stop_native_process(const OwnedResourceIdentity& resource) {
  const auto pid = process_id(resource);
  if (!native_process_status(resource)) return;
  ::kill(pid, SIGTERM);
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (!native_process_status(resource)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (native_process_status(resource)) ::kill(pid, SIGKILL);
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (!native_process_status(resource)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  throw std::runtime_error("E_PROCESS_TIMEOUT: owned process did not stop");
}
OwnedResourceIdentity start_native_process(
    const NativeProcessOptions& options,
    const std::function<void(const OwnedResourceIdentity&)>& register_identity) {
  safe_execution_path(options.executable);
  safe_execution_path(options.cwd);
  safe_execution_path(options.log, true);
  const auto digest = configuration_hash(options.executable);
  const int log = ::open(options.log.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (log < 0) throw std::runtime_error("E_PROCESS_LOG: exclusive log creation failed");
  std::array<int, 2> gate{};
  if (::pipe(gate.data()) != 0) {
    ::close(log);
    throw std::runtime_error("E_PROCESS_START: cannot create registration gate");
  }
  std::array<int, 2> executed{};
  if (::pipe(executed.data()) != 0) {
    ::close(log);
    ::close(gate[0]);
    ::close(gate[1]);
    throw std::runtime_error("E_PROCESS_START: cannot create exec confirmation pipe");
  }
  if (::fcntl(executed[1], F_SETFD, FD_CLOEXEC) != 0) {
    ::close(log);
    ::close(gate[0]);
    ::close(gate[1]);
    ::close(executed[0]);
    ::close(executed[1]);
    throw std::runtime_error("E_PROCESS_START: cannot configure exec confirmation pipe");
  }
  int network_namespace = -1;
  if (!options.network_namespace.empty()) {
#if defined(__linux__)
    network_namespace =
        ::open(options.network_namespace.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat metadata{};
    if (network_namespace < 0 || ::fstat(network_namespace, &metadata) != 0 ||
        static_cast<std::uint64_t>(metadata.st_ino) != options.namespace_inode) {
      if (network_namespace >= 0) ::close(network_namespace);
      ::close(log);
      ::close(gate[0]);
      ::close(gate[1]);
      ::close(executed[0]);
      ::close(executed[1]);
      throw std::runtime_error("E_NAMESPACE_IDENTITY: cannot enter replaced namespace");
    }
#else
    ::close(log);
    ::close(gate[0]);
    ::close(gate[1]);
    ::close(executed[0]);
    ::close(executed[1]);
    throw std::runtime_error("E_TARGET: namespace processes require Linux");
#endif
  }
  const auto pid = ::fork();
  if (pid == 0) {
    ::close(executed[0]);
    ::close(gate[1]);
    char release{};
    if (::read(gate[0], &release, 1) != 1 || release != 'R') ::_exit(125);
    ::close(gate[0]);
#if defined(__linux__)
    if (network_namespace >= 0 && (::setns(network_namespace, CLONE_NEWNET) != 0 ||
                                   ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0))
      ::_exit(125);
#endif
    if (::setsid() < 0 || ::chdir(options.cwd.c_str()) != 0) ::_exit(125);
    const int null = ::open("/dev/null", O_RDONLY);
    if (null < 0 || ::dup2(null, STDIN_FILENO) < 0 || ::dup2(log, STDOUT_FILENO) < 0 ||
        ::dup2(log, STDERR_FILENO) < 0)
      ::_exit(125);
    const auto maximum = ::sysconf(_SC_OPEN_MAX);
    for (int fd = 3; fd < maximum; ++fd)
      if (fd != executed[1]) ::close(fd);
    if (options.file_bytes_limit) {
      const rlimit limit{static_cast<rlim_t>(options.file_bytes_limit),
                         static_cast<rlim_t>(options.file_bytes_limit)};
      if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) ::_exit(125);
    }
    ::signal(SIGINT, SIG_DFL);
    ::signal(SIGTERM, SIG_DFL);
    ::signal(SIGPIPE, SIG_DFL);
    ::signal(SIGXFSZ, SIG_DFL);
    // Only the explicit execution projection reaches applications.
    std::vector<std::string> environment;
    for (const auto& [key, value] : options.environment) environment.push_back(key + "=" + value);
    std::vector<char*> envp;
    for (auto& value : environment) envp.push_back(value.data());
    envp.push_back(nullptr);
    std::vector<char*> argv;
    for (const auto& argument : options.argv) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execve(argv.front(), argv.data(), envp.data());
    ::_exit(127);
  }
  ::close(executed[1]);
  if (network_namespace >= 0) ::close(network_namespace);
  ::close(log);
  ::close(gate[0]);
  if (pid < 0) {
    ::close(executed[0]);
    ::close(gate[1]);
    throw std::runtime_error("E_PROCESS_START: fork failed");
  }
  OwnedResourceIdentity resource;
  resource.kind = "native";
  resource.name = options.id;
  resource.stable_id = std::to_string(pid);
  resource.secondary_id = inspect_process(static_cast<std::uint32_t>(pid)).start_time;
  resource.process_identity = options.executable.string() + "\n" + digest;
  try {
    if (resource.secondary_id.empty()) throw std::runtime_error("E_PROCESS_IDENTITY: unavailable");
    register_identity(resource);
    if (::write(gate[1], "R", 1) != 1) throw std::runtime_error("E_PROCESS_START: gate failed");
    ::close(gate[1]);
    gate[1] = -1;
    pollfd completion{executed[0], POLLIN | POLLHUP, 0};
    const auto confirmed = ::poll(&completion, 1, 10000);
    if (confirmed < 0 && errno == EINTR)
      throw std::runtime_error(
          "E_INTERRUPTED: process startup interrupted before exec confirmation");
    if (confirmed <= 0) throw std::runtime_error("E_PROCESS_START: exec confirmation timed out");
    ::close(executed[0]);
    executed[0] = -1;
  } catch (...) {
    if (executed[0] >= 0) ::close(executed[0]);
    if (gate[1] >= 0) ::close(gate[1]);
    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
    throw;
  }
  return resource;
}
}  // namespace graphx::infra::detail
