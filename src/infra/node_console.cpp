#include "infra/ownership_lock.hpp"
#include "infra/node_console.hpp"
#include "infra/command_runner.hpp"
#include "infra/process_resources.hpp"
#include "infra/qemu_resources.hpp"
#include "config_document.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
namespace graphx::infra::detail {
namespace {
using Value = ConfigValue;
using Object = Value::Object;
constexpr std::size_t limit = 65536;
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
std::string hex(std::string_view bytes) {
  const char* digits = "0123456789abcdef";
  std::string result;
  for (unsigned char c : bytes) {
    result += digits[c >> 4];
    result += digits[c & 15];
  }
  return result;
}
std::string tail(const std::filesystem::path& path) {
  safe_execution_path(path);
  const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) throw std::runtime_error("log unavailable");
  struct stat st{};
  if (::fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
    ::close(fd);
    throw std::runtime_error("invalid log identity");
  }
  std::string result(std::min<std::size_t>(limit, static_cast<std::size_t>(st.st_size)), '\0');
  const auto n = ::pread(fd, result.data(), result.size(), std::max<off_t>(0, st.st_size - limit));
  ::close(fd);
  if (n < 0) throw std::runtime_error("cannot read log");
  result.resize(static_cast<std::size_t>(n));
  return result;
}
void directory_identity(const std::filesystem::path& path, const OwnershipState& state) {
  safe_execution_path(path);
  struct stat st{};
  if (::lstat(path.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid() ||
      static_cast<std::uint64_t>(st.st_ino) != state.console_inode ||
      static_cast<std::uint64_t>(st.st_dev) != state.console_device)
    throw std::runtime_error("E_CONSOLE_IDENTITY: directory replaced");
}
}  // namespace
void prepare_node_console(const std::filesystem::path& root, OwnershipState& state) {
  if (!state.console_name.empty()) {
    const auto previous = root / state.console_name;
    directory_identity(previous, state);
    // The caller has checked that all old owned processes have stopped.
    for (const auto& entry : std::filesystem::directory_iterator(previous)) {
      struct stat st{};
      const auto name = entry.path().filename().string();
      const auto socket =
          state.console_sockets.find(name.substr(0, name.size() > 5 ? name.size() - 5 : 0));
      if (::lstat(entry.path().c_str(), &st) ||
          !((S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_uid == ::geteuid() &&
             (name.ends_with(".json") || name.ends_with(".boot") || name == "relay.log" ||
              name == "relay.bin")) ||
            (S_ISSOCK(st.st_mode) && st.st_uid == 65532 && name.ends_with(".sock") &&
             socket != state.console_sockets.end() &&
             socket->second == std::to_string(st.st_dev) + ":" + std::to_string(st.st_ino))))
        throw std::runtime_error("E_CONSOLE_IDENTITY: unexpected retained file");
    }
    for (const auto& entry : std::filesystem::directory_iterator(previous))
      std::filesystem::remove(entry.path());
    std::filesystem::remove(previous);
  }
  state.console_sockets.clear();
  state.console_name = "console-" + random_token();
  state.console_inode = state.console_device = 0;
  save_state(root / "ownership.yml", state);
  const auto path = root / state.console_name;
  if (!std::filesystem::create_directory(path))
    throw std::runtime_error("console directory collision");
  if (::geteuid() == 0 && ::chown(path.c_str(), 0, 65532))
    throw std::runtime_error("cannot assign console group");
  if (::chmod(path.c_str(), ::geteuid() == 0 ? 0770 : 0755))
    throw std::runtime_error("cannot set console permissions");
  struct stat st{};
  if (::lstat(path.c_str(), &st)) throw std::runtime_error("cannot inspect console directory");
  state.console_inode = static_cast<std::uint64_t>(st.st_ino);
  state.console_device = static_cast<std::uint64_t>(st.st_dev);
  save_state(root / "ownership.yml", state);
}
void start_node_console(const ExecutionOptions& options, OwnershipState& state) {
  NativeProcessOptions start;
#if defined(__APPLE__)
  char executable[4096];
  std::uint32_t length = sizeof(executable);
  if (_NSGetExecutablePath(executable, &length))
    throw std::runtime_error("cannot locate console relay");
  start.executable = std::filesystem::canonical(executable);
#else
  start.executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
  if (!options.release.empty()) {
    start.executable = options.release / "bin/graphx";
  } else {
    const auto copy = options.state_root / state.graph_id / state.console_name / "relay.bin";
    const auto digest = configuration_hash(start.executable);
    if (!std::filesystem::copy_file(start.executable, copy) || configuration_hash(copy) != digest ||
        ::chmod(copy.c_str(), 0555) != 0)
      throw std::runtime_error("E_CONSOLE_IDENTITY: cannot stage stable relay executable");
    start.executable = copy;
  }
  start.id = "mg-node-console";
  start.cwd = options.output;
  start.log = options.state_root / state.graph_id / state.console_name / "relay.log";
  const char* path = std::getenv("PATH");
  start.environment = {{"PATH", path ? path : "/usr/bin:/bin"}};
  for (const auto* key : {"DOCKER_HOST", "DOCKER_CONTEXT", "DOCKER_CONFIG", "HOME"})
    if (const char* value = std::getenv(key)) start.environment[key] = value;
  start.argv = {start.executable.string(),
                "run",
                "console-relay",
                "--output",
                options.output.string(),
                "--state-root",
                options.state_root.string(),
                "--owner",
                state.owner_token};
  start_native_process(start, [&](const auto& process) {
    state.processes.push_back(process);
    save_state(options.state_root / state.graph_id / "ownership.yml", state);
  });
}
int execute_node_console(const ExecutionOptions& options, const Value& resolved) {
  if (options.owner_token.empty()) throw std::runtime_error("missing console owner");
  const auto graph = resolved.at("graph_id").text();
  const auto root = options.state_root / graph;
  const auto hash = configuration_hash(options.output / "compile-manifest.json");
  stopped = 0;
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  while (!stopped) {
    try {
      // The ledger is atomically published. This read-only observer never takes
      // the mutation lock; teardown stops this owned child before removing sources.
      const auto state = load_state(root / "ownership.yml");
      if (state.owner_token != options.owner_token || state.graph_id != graph ||
          state.config_hash != hash)
        throw std::runtime_error("E_CONSOLE_IDENTITY: owner changed");
      const auto self = std::ranges::find_if(state.processes, [](const auto& p) {
        return p.name == "mg-node-console" && p.stable_id == std::to_string(::getpid());
      });
      if (self == state.processes.end() || !native_process_status(*self)) return 1;
      if (state.status != "ready") {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      const auto directory = root / state.console_name;
      directory_identity(directory, state);
      for (const auto& node : resolved.at("nodes").array()) {
        const auto id = node.at("node_id").text();
        const auto kind = node.at("execution").at("kind").text();
        Object result{{"version", 1},
                      {"graph", graph},
                      {"node", id},
                      {"generation", state.console_name},
                      {"status", "unavailable"},
                      {"source", "stdout/stderr (merged)"},
                      {"hex", ""},
                      {"updated", static_cast<std::int64_t>(std::time(nullptr))},
                      {"bounded", true}};
        const auto process = std::ranges::find_if(state.processes, [&](const auto& p) {
          return p.name == id || p.name == "graphx-" + graph + "-" + id;
        });
        if (process != state.processes.end()) try {
            std::string bytes;
            bool live{};
            if (process->kind == "container") {
              CommandOptions command;
              command.arguments = {"docker", "inspect", process->stable_id};
              command.capture_output = true;
              command.timeout_ms = 2000;
              auto inspected = run_command(command);
              if (inspected.status || inspected.output_truncated)
                throw std::runtime_error("container unavailable");
              const auto object = config_internal::parse_document(inspected.output).array().front();
              if (object.at("Id") != Value(process->stable_id) ||
                  object.at("Image") != Value(process->secondary_id) ||
                  object.at("Config").at("Labels").at("org.graphx.owner") !=
                      Value(state.owner_token) ||
                  object.at("Config").at("Labels").at("org.graphx.graph") != Value(graph))
                throw std::runtime_error("container identity changed");
              live = object.at("State").at("Running").boolean();
              command.arguments = {"docker", "logs", "--tail", "256", process->stable_id};
              command.output_limit = limit;
              command.merge_standard_error = true;
              command.trim_trailing_newlines = false;
              auto logs = run_command(command);
              if (logs.status) throw std::runtime_error("container logs unavailable");
              bytes = logs.output;
            } else {
              live = native_process_status(*process) != 0;
              if (kind == "qemu") {
                verify_guest_directory(*process);
                const auto expected_socket = state.console_sockets.find(id);
                struct stat serial_socket{};
                if (expected_socket == state.console_sockets.end() ||
                    ::lstat((directory / (id + ".sock")).c_str(), &serial_socket) ||
                    !S_ISSOCK(serial_socket.st_mode) || serial_socket.st_uid != 65532 ||
                    expected_socket->second != std::to_string(serial_socket.st_dev) + ":" +
                                                   std::to_string(serial_socket.st_ino))
                  throw std::runtime_error("E_CONSOLE_IDENTITY: serial socket replaced");
                result["serial_identity"] = expected_socket->second;
                if (live) {
                  auto lock =
                      OwnershipLock::open_existing(root / ".lock", OwnershipLockMode::shared);
                  (void)inspect_guest(*process, false, true);
                }
                bytes = tail(directory / (id + ".boot"));
                result["source"] = "guest boot serial (ttyS0)";
                result["diagnostics_hex"] =
                    hex(tail(std::filesystem::path(process->runtime_directory) / "qemu.log"));
              } else
                bytes = tail(root / "logs" / (id + ".log"));
            }
            result["status"] = live ? "running" : "stopped";
            result["hex"] = hex(bytes);
            result["runtime"] = process->stable_id + ":" + process->secondary_id;
          } catch (const std::runtime_error& error) {
            if (std::string_view(error.what()) ==
                "another infrastructure operation owns the graph lock")
              continue;
            result["status"] = "unavailable";
          }
        publish_execution_file(directory / (id + ".json"), config_value_json(Value(result)), 0444);
      }
    } catch (const std::runtime_error& error) {
      if (std::string_view(error.what()) != "another infrastructure operation owns the graph lock")
        throw;
    }
    for (int i = 0; i < 10 && !stopped; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return 0;
}
}  // namespace graphx::infra::detail
