#include "infra/node_console.hpp"
#include "infra/application_lifecycle.hpp"
#include "graphx/execution.hpp"
#include "graphx/ownership.hpp"
#include "graphx/config_schemas.hpp"
#include "graphx/version.hpp"
#include "graphx/node_settings.hpp"
#include "config_document.hpp"
#include "infra/ownership_lock.hpp"
#include "infra/compose_execution.hpp"
#include "infra/scenario_execution.hpp"
#include "infra/process_resources.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <regex>
#include <set>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace graphx {
namespace {
using namespace config_internal;
using namespace infra::detail;
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }
void checkpoint() {
  if (interrupted) throw std::runtime_error("E_INTERRUPTED: startup interrupted before release");
}
Value document(const std::filesystem::path& path) {
  safe_execution_path(path);
  return parse_document(read_document(path, 16 * 1024 * 1024));
}
CommandResult command(std::vector<std::string> argv, std::string input = {},
                      std::uint32_t timeout = 30000, bool required = true) {
  CommandOptions opts;
  opts.arguments = std::move(argv);
  opts.standard_input = std::move(input);
  opts.capture_output = true;
  opts.output_limit = 4 * 1024 * 1024;
  opts.timeout_ms = timeout;
  opts.cancellation_requested = [] { return interrupted != 0; };
  auto result = run_command(opts);
  if (required && (result.status || result.exec_error || result.output_truncated))
    throw std::runtime_error("E_EXECUTION_COMMAND: " + opts.arguments.front() + " failed");
  return result;
}
bool healthy(std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  const timeval timeout{0, 200000};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &endpoint.sin_addr);
  bool ready{};
  if (::connect(fd, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == 0) {
    const std::string request = "GET /api/ready HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    if (::send(fd, request.data(), request.size(), 0) > 0) {
      char bytes[1024]{};
      const auto count = ::recv(fd, bytes, sizeof(bytes), 0);
      ready = count > 0 &&
              std::string_view(bytes, static_cast<std::size_t>(count)).starts_with("HTTP/1.1 200");
    }
  }
  ::close(fd);
  return ready;
}
void verify_compilation(const std::filesystem::path& output, const Value& manifest) {
  if (manifest.at("compiler") != Value("graphx-compiler-1") ||
      manifest.at("compiler_version") != Value(std::string(version)))
    throw std::runtime_error("E_COMPILE_IDENTITY: unsupported compiler");
  std::set<std::string> expected{"compile-manifest.json"};
  for (const auto& file : manifest.at("files").array()) {
    const auto relative = file.at("path").text();
    const auto path = output / relative;
    if (std::filesystem::path(relative).is_absolute() || path.lexically_normal() != path ||
        !expected.insert(relative).second)
      throw std::runtime_error("E_COMPILE_IDENTITY: invalid artifact inventory");
    safe_execution_path(path);
    if (configuration_hash(path) != file.at("sha256").text())
      throw std::runtime_error("E_COMPILE_IDENTITY: artifact digest mismatch");
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(output)) {
    safe_execution_path(entry.path());
    if (entry.is_regular_file() &&
        !expected.contains(entry.path().lexically_relative(output).string()))
      throw std::runtime_error("E_COMPILE_IDENTITY: unlisted artifact");
  }
}
void verify_native_release(const std::filesystem::path& release) {
  const auto receipt = document(release / "release.json");
  if (receipt.at("version") != Value(1) ||
      receipt.at("graphx_version") != Value(std::string(version)))
    throw std::runtime_error("E_RELEASE_IDENTITY: unsupported installed release");
#if defined(__APPLE__)
  const std::string os = "darwin-";
#else
  const std::string os = "linux-";
#endif
#if defined(__aarch64__) || defined(__arm64__)
  const std::string architecture = "aarch64";
#else
  const std::string architecture = "x86_64";
#endif
  if (receipt.at("platform") != Value(os + architecture))
    throw std::runtime_error("E_RELEASE_IDENTITY: host platform mismatch");
  for (const auto& [name, entry] : receipt.at("files").object()) {
    const auto path = release / name;
    if (std::filesystem::path(name).is_absolute() || path.lexically_normal() != path)
      throw std::runtime_error("E_RELEASE_IDENTITY: unsafe installed member");
    safe_execution_path(path);
    struct stat metadata{};
    if (::lstat(path.c_str(), &metadata) != 0 ||
        (metadata.st_mode & 0777) != entry.at("mode").integer() ||
        configuration_hash(path) != entry.at("sha256").text())
      throw std::runtime_error("E_RELEASE_IDENTITY: installed member changed");
  }
  for (const auto& item : std::filesystem::recursive_directory_iterator(release)) {
    if (item.is_symlink() || (!item.is_directory() && !item.is_regular_file()))
      throw std::runtime_error("E_RELEASE_IDENTITY: unsupported installed member");
    if (item.is_regular_file() && item.path() != release / "release.json" &&
        !receipt.at("files").contains(item.path().lexically_relative(release).string()))
      throw std::runtime_error("E_RELEASE_IDENTITY: unlisted installed member");
  }
  for (const auto* required : {"bin/graphx", "bin/graphx-platform", "libexec/graphx-platform/node"})
    if (!receipt.at("files").contains(required))
      throw std::runtime_error("E_RELEASE_IDENTITY: incomplete native installation");
}
}  // namespace

int execute_graph(const ExecutionOptions& opts, std::ostream& output) {
  if (opts.action != "up" && opts.action != "down" && opts.action != "status" &&
      opts.action != "plan" && opts.action != "console-relay" && opts.action != "handoff" &&
      !opts.action.starts_with("scenario-"))
    throw std::invalid_argument("run requires plan, up, down or status");
  const auto manifest = document(opts.output / "compile-manifest.json");
  verify_compilation(opts.output, manifest);
  const auto resolved = document(opts.output / "resolved.json");
  validate_shape(resolved, parse_document(normalized_schema));
  const auto id = resolved.at("graph_id").text();
  if (!std::regex_match(id, std::regex("^[a-z][a-z0-9_-]{0,63}$")))
    throw std::runtime_error("E_EXECUTION_ID: unsafe graph identity");
  const auto state_directory = opts.state_root / id;
  if (opts.action.starts_with("scenario-")) {
    if (opts.action == "scenario-run" && !opts.release.empty()) verify_native_release(opts.release);
    return execute_scenario(opts, resolved, output);
  }
  if (opts.action == "console-relay") return execute_node_console(opts, resolved);
  if (opts.action == "handoff") return execute_capture_handoff(opts, resolved, output);
  if (opts.action == "plan") {
    GraphConfig config;
    config.id = id;
    config.version = 3;
    config.resolved = resolved;
    config.deployment.project = "graphx-" + id;
    config.network_infrastructure = resolved_network(resolved.at("network"));
    return execute_ovs_lifecycle(config, opts.output / "compile-manifest.json",
                                 OvsLifecycleAction::create, true, opts.state_root, output, output);
  }
  if (opts.action == "up") {
    const auto overlaps = [](const auto& a, const auto& b) {
      if (a.empty() || b.empty()) return false;
      const auto inside = [](const auto& child, const auto& parent) {
        const auto relative = child.lexically_relative(parent);
        return !relative.empty() && *relative.begin() != "..";
      };
      return inside(a, b) || inside(b, a);
    };
    for (const auto& immutable : {opts.output, opts.release, opts.images})
      if (overlaps(state_directory, immutable) || overlaps(opts.credentials, immutable))
        throw std::runtime_error("E_EXECUTION_PATH: runtime roots overlap immutable artifacts");
  }
  const auto state_file = state_directory / "ownership.yml";
  const auto config_hash = configuration_hash(opts.output / "compile-manifest.json");
  const bool native = resolved.at("platform").at("telemetry").at("host") == Value("127.0.0.1");
  if (!native) {
    if (opts.action == "up" &&
        std::ranges::any_of(resolved.at("nodes").array(), [](const auto& node) {
          return node.at("execution").at("kind") == Value("namespace") ||
                 node.at("execution").at("kind") == Value("qemu");
        }))
      verify_native_release(opts.release);
    return execute_compose(opts, resolved, output);
  }
  if (opts.action == "up") {
#if defined(__APPLE__)
    if (resolved.at("target") != Value("native-macos"))
      throw std::runtime_error("E_TARGET: native target does not match macOS host");
#else
    if (resolved.at("target") != Value("native-linux") && resolved.at("target") != Value("lima"))
      throw std::runtime_error("E_TARGET: native target does not match Linux host");
#endif
    if (!resolved.at("network").at("switches").array().empty())
      throw std::runtime_error(
          "E_PHASE_UNAVAILABLE: native applications cannot own OVS endpoints; use container or "
          "namespace placement");
    for (const auto& node : resolved.at("nodes").array())
      if (node.at("execution").at("kind") != Value("native"))
        throw std::runtime_error("E_PHASE_UNAVAILABLE: graph requires a later execution adapter");
    verify_native_release(opts.release);
    for (const auto& node : resolved.at("nodes").array())
      if (load_node_settings(opts.output / "nodes" / (node.at("node_id").text() + ".json"),
                             node.at("node_id").text())
              .resolved != node)
        throw std::runtime_error(
            "E_COMPILE_IDENTITY: node projection disagrees with resolved graph");
    safe_execution_path(opts.credentials, true);
    safe_execution_path(state_directory, true);
    require_available_tcp_port(
        static_cast<std::uint16_t>(resolved.at("platform").at("console").at("port").integer()));
    ensure_state_root(opts.state_root);
    ensure_state_root(state_directory);
  } else if (!std::filesystem::exists(state_file)) {
    output << "inactive graph=" << id << '\n';
    return 0;
  }
  safe_execution_path(state_directory);
  auto lock =
      OwnershipLock::open_or_create(state_directory / ".lock", OwnershipLockMode::exclusive);
  OwnershipState state;
  if (std::filesystem::exists(state_file)) {
    state = load_state(state_file);
    if (state.graph_id != id || state.config_hash != config_hash)
      throw std::runtime_error("E_EXECUTION_IDENTITY: state belongs to another compiled graph");
  } else {
    state.graph_id = id;
    state.config_hash = config_hash;
    state.owner_token = random_token();
    state.status = "creating";
  }
  const auto save = [&] { save_state(state_file, state); };
  const auto barrier = state_directory / "barriers/release";
  const auto remove_barrier = [&] {
    if (std::filesystem::exists(barrier)) {
      safe_execution_path(barrier);
      if (read_document(barrier, 128) != state.owner_token)
        throw std::runtime_error("E_EXECUTION_IDENTITY: release barrier identity mismatch");
      std::filesystem::remove(barrier);
    }
  };
  const auto stop = [&] {
    // Validate the entire inventory before the first destructive operation.
    const auto identity_deadline = Clock::now() + std::chrono::milliseconds(500);
    for (;;) {
      try {
        for (const auto& process : state.processes) native_process_status(process);
        break;
      } catch (...) {
        // exec preserves PID/start time but temporarily changes the platform's
        // executable during lock handoff. Never signal until final identity matches.
        if (Clock::now() >= identity_deadline) throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    remove_barrier();
    state.status = "destroying";
    save();
    while (!state.processes.empty()) {
      stop_native_process(state.processes.back());
      state.processes.pop_back();
      save();
    }
  };
  if (opts.action == "status" && available_startup(resolved) && !state.processes.empty()) {
    std::map<std::string, bool> live;
    bool platform_live = false;
    for (const auto& process : state.processes) {
      const bool running = native_process_status(process) != 0;
      if (process.name == "platform") platform_live = running;
      if (state.applications.contains(process.name)) {
        live[process.name] = running && state.applications.at(process.name) == "ready";
        output << "application=" << process.name
               << " admission=" << state.applications.at(process.name)
               << " status=" << (live[process.name] ? "ready" : "unavailable") << '\n';
      }
    }
    output << "graph=" << id
           << " status=" << (platform_live ? application_status(resolved, live) : "unavailable")
           << " evidence=process-liveness throughput=unverified\n";
    return 0;
  }
  if (opts.action == "status") {
    for (const auto& process : state.processes)
      output << process.name << ' ' << (native_process_status(process) ? "running" : "exited")
             << '\n';
    output << "graph=" << id << " status=" << (state.processes.empty() ? "inactive" : state.status)
           << '\n';
    return 0;
  }
  if (opts.action == "down") {
    stop();
    output << "stopped graph=" << id << "; history and logs retained\n";
    return 0;
  }
  if (!state.processes.empty())
    throw std::runtime_error("E_EXECUTION_ACTIVE: recover with run down before another startup");
  remove_barrier();
  ensure_state_root(state_directory / "logs");
  if (resolved.at("platform").at("capture").at("enabled").boolean()) {
    ensure_state_root(state_directory / "captures");
    for (const auto& node : resolved.at("nodes").array())
      if (node.at("capture").at("enabled").boolean())
        ensure_state_root(state_directory / "captures" / node.at("node_id").text());
  }
  ensure_state_root(state_directory / "barriers");
  state.status = "creating";
  state.applications.clear();
  save();
  prepare_node_console(state_directory, state);
  interrupted = 0;
  const auto old_int = std::signal(SIGINT, interrupt);
  const auto old_term = std::signal(SIGTERM, interrupt);
  const auto old_pipe = std::signal(SIGPIPE, SIG_IGN);
  const auto restore = [&] {
    std::signal(SIGINT, old_int);
    std::signal(SIGTERM, old_term);
    std::signal(SIGPIPE, old_pipe);
  };
  try {
    const auto node = opts.release / "libexec/graphx-platform/node";
    const auto platform = opts.release / "libexec/graphx-platform/apps/telemetry/platform.mjs";
    if (!std::filesystem::exists(opts.credentials)) {
      std::vector<std::string> stage{node.string(),
                                     platform.string(),
                                     "stage",
                                     "--manifest",
                                     (opts.output / "credentials.json").string(),
                                     "--destination",
                                     opts.credentials.string()};
      if (!opts.external_credentials.empty()) {
        safe_execution_path(opts.external_credentials);
        stage.insert(stage.end(), {"--external", opts.external_credentials.string()});
      }
      command(stage);
    }
    struct stat credential_metadata{};
    safe_execution_path(opts.credentials);
    if (::lstat(opts.credentials.c_str(), &credential_metadata) != 0 ||
        !S_ISDIR(credential_metadata.st_mode))
      throw std::runtime_error("E_CREDENTIAL_IDENTITY: staging directory unavailable");
    state.credential_directory = opts.credentials.string();
    state.credential_identity = std::to_string(credential_metadata.st_dev) + ":" +
                                std::to_string(credential_metadata.st_ino);
    state.actions.clear();
    save();
    const std::map<std::string, std::string> replacements{
        {"GX_RELEASE", opts.release.string()},
        {"GX_OUTPUT", opts.output.string()},
        {"GX_STATE", state_directory.string()},
        {"GX_CREDENTIALS", opts.credentials.string()},
        {"GX_OWNER", state.owner_token}};
    auto processes = document(opts.output / "native-plan.json").at("processes").array();
    if (processes.size() != resolved.at("nodes").array().size() + 1)
      throw std::runtime_error("E_COMPILE_IDENTITY: process inventory mismatch");
    std::set<std::string> started_names;
    std::stable_sort(processes.begin(), processes.end(), [](const Value& a, const Value& b) {
      return a.at("id") == Value("platform") && b.at("id") != Value("platform");
    });
    for (const auto& process : processes) {
      checkpoint();
      const auto name = process.at("id").text();
      if (!started_names.insert(name).second)
        throw std::runtime_error("E_COMPILE_IDENTITY: duplicate process");
      NativeProcessOptions start;
      start.id = name;
      start.cwd = opts.output;
      start.log = state_directory / "logs" / (name + ".log");
      if (std::filesystem::exists(start.log)) {
        safe_execution_path(start.log);
        const auto previous = std::filesystem::path(start.log.string() + ".previous");
        safe_execution_path(previous, true);
        std::filesystem::rename(start.log, previous);
      }
      if (name == "platform") {
        start.argv = {node.string(), platform.string(), "--config",
                      (opts.output / "platform.json").string()};
        start.executable = node;
      } else {
        const auto found = std::ranges::find_if(
            resolved.at("nodes").array(),
            [&](const auto& candidate) { return candidate.at("node_id") == Value(name); });
        if (found == resolved.at("nodes").array().end())
          throw std::runtime_error("E_COMPILE_IDENTITY: unknown process");
        const auto types = parse_document(runtime_types);
        const auto type = std::ranges::find_if(types.array(), [&](const auto& candidate) {
          return candidate.at("id") == found->at("type");
        });
        if (type == types.array().end())
          throw std::runtime_error("E_RELEASE_IDENTITY: unsupported executable");
        start.executable = opts.release / "bin" / type->at("executable").text();
        if (!document(opts.release / "release.json")
                 .at("files")
                 .contains(start.executable.lexically_relative(opts.release).string()))
          throw std::runtime_error(
              "E_RELEASE_IDENTITY: executable has no verified inventory entry");
        start.argv = {start.executable.string(),
                      "--node",
                      name,
                      "--config",
                      (opts.output / "nodes" / (name + ".json")).string(),
                      "--release-file",
                      barrier.string(),
                      "--release-token",
                      state.owner_token};
        start.environment["GRAPHX_CREDENTIALS"] = opts.credentials.string();
        const auto& credential = found->at("telemetry").at("credential");
        if (!credential.is_null())
          start.environment["GRAPHX_TELEMETRY_SHARED_SECRET_FILE"] =
              (opts.credentials / credential.text() / "hmac").string();
      }
      if (name == "platform")
        start.environment["GX_CONSOLE"] = (state_directory / state.console_name).string();
      for (const auto& [key, value] : replacements) start.environment[key] = value;
      start.environment["PATH"] = (opts.release / "bin").string() + ":/usr/bin:/bin";
      start.environment["HOME"] =
          std::getenv("HOME") ? std::getenv("HOME") : state_directory.string();
      start.environment["GRAPHX_LOG_MAX_BYTES"] = std::to_string(start.log_bytes);
      start_native_process(start, [&](const OwnedResourceIdentity& resource) {
        state.processes.push_back(resource);
        save();
      });
      if (available_startup(resolved) && name != "platform") continue;
      const auto deadline =
          Clock::now() + std::chrono::milliseconds(
                             name == "platform" ? 30000 : application_readiness_ms(resolved));
      while (true) {
        checkpoint();
        if (Clock::now() >= deadline) throw std::runtime_error("E_READINESS_TIMEOUT: " + name);
        const auto identity = inspect_process(
            static_cast<std::uint32_t>(std::stoul(state.processes.back().stable_id)));
        int exit_status{};
        if (::waitpid(static_cast<pid_t>(std::stoul(state.processes.back().stable_id)),
                      &exit_status, WNOHANG) > 0)
          throw std::runtime_error("E_READINESS_EXIT: " + name +
                                   " status=" + std::to_string(exit_status));
        const auto log = read_process_log(start.log, start.log_bytes + 1);
        if ((name == "platform" && identity.executable == node &&
             healthy(static_cast<std::uint16_t>(
                 resolved.at("platform").at("console").at("port").integer()))) ||
            (name != "platform" && application_ready(log, name)))
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    if (available_startup(resolved)) {
      const auto deadline =
          Clock::now() + std::chrono::milliseconds(application_readiness_ms(resolved));
      while (true) {
        checkpoint();
        bool pending = false;
        for (const auto& process : state.processes) {
          if (process.name == "platform") {
            if (!native_process_status(process))
              throw std::runtime_error("E_READINESS_EXIT: platform");
            continue;
          }
          if (state.applications.contains(process.name)) continue;
          int status{};
          const bool running = native_process_status(process, &status) != 0;
          if (!running) {
            if (status < 0 || (!WIFSIGNALED(status) &&
                               (!WIFEXITED(status) || !unavailable_exit(WEXITSTATUS(status)))))
              throw std::runtime_error("E_APPLICATION_STARTUP: unclassified failure " +
                                       process.name);
            state.applications[process.name] = "exited";
          } else if (Clock::now() < deadline &&
                     application_ready(
                         read_process_log(state_directory / "logs" / (process.name + ".log"),
                                          2097152),
                         process.name)) {
            state.applications[process.name] = "ready";
          } else if (Clock::now() >= deadline) {
            stop_native_process(process);
            state.applications[process.name] = "timeout";
          } else
            pending = true;
        }
        save();
        if (!pending) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    for (const auto& process : state.processes) {
      int status{};
      if (native_process_status(process, &status)) continue;
      if (!available_startup(resolved) || process.name == "platform")
        throw std::runtime_error("E_READINESS_EXIT: application exited before release");
      if (state.applications.at(process.name) == "ready") {
        if (status < 0 || (!WIFSIGNALED(status) &&
                           (!WIFEXITED(status) || !unavailable_exit(WEXITSTATUS(status)))))
          throw std::runtime_error("E_APPLICATION_STARTUP: unclassified failure " + process.name);
        state.applications[process.name] = "exited";
      }
    }
    checkpoint();
    publish_execution_file(barrier, state.owner_token, 0444);
    state.status = "ready";
    save();
    start_node_console(opts, state);
    std::map<std::string, bool> live;
    for (const auto& [name, admission] : state.applications) live[name] = admission == "ready";
    output << (available_startup(resolved) ? application_status(resolved, live) : "ready")
           << " graph=" << id << " owner=" << state.owner_token << "\n";
    restore();
    return 0;
  } catch (...) {
    try {
      stop();
    } catch (const std::exception& error) {
      std::cerr << error.what() << "; recovery state retained\n";
    }
    restore();
    throw;
  }
}
}  // namespace graphx
