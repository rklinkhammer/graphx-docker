#include "infra/node_console.hpp"
#include "infra/application_lifecycle.hpp"
#include "infra/compose_execution.hpp"
#include "infra/qemu_resources.hpp"
#include "infra/process_resources.hpp"
#include "infra/ownership_lock.hpp"
#include "infra/lifecycle_coordinator.hpp"
#include "infra/management_policy.hpp"
#include "infra/namespace_resources.hpp"
#include "config_document.hpp"
#include "graphx/config_schemas.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace graphx::infra::detail {
namespace {
using namespace config_internal;
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t cancelled{};
void cancel(int) { cancelled = 1; }
Value read(const std::filesystem::path& path) {
  safe_execution_path(path);
  return parse_document(read_document(path, 16 * 1024 * 1024));
}
std::string call(std::vector<std::string> arguments, std::string input = {}, bool required = true,
                 std::uint32_t timeout = 30000) {
  CommandOptions opts;
  opts.merge_standard_error = arguments.size() > 1 && arguments[1] == "logs";
  opts.cancellation_requested = [] { return cancelled != 0; };
  opts.arguments = std::move(arguments);
  opts.capture_output = true;
  opts.standard_input = std::move(input);
  opts.output_limit = 4 * 1024 * 1024;
  opts.timeout_ms = timeout;
  const auto result = run_command(opts);
  if (result.status || result.exec_error || result.output_truncated) {
    if (required) throw std::runtime_error("E_DOCKER_COMMAND: " + format_arguments(opts.arguments));
    return {};
  }
  return result.output;
}
Value inspect(const std::string& kind, const std::string& id) {
  const auto result =
      call({"docker", kind == "container" ? "container" : kind, "inspect", id}, {}, false);
  if (result.empty()) return {};
  const auto objects = parse_document(result);
  if (objects.array().size() != 1)
    throw std::runtime_error("E_DOCKER_IDENTITY: ambiguous identity");
  return objects.array().front();
}
const Value& labels(const OwnedResourceIdentity& resource, const Value& object) {
  return resource.kind == "container" ? object.at("Config").at("Labels") : object.at("Labels");
}
bool observe(OwnedResourceIdentity& resource, const std::string& graph) {
  const auto object =
      inspect(resource.kind, resource.stable_id == "pending" ? resource.name : resource.stable_id);
  if (object.is_null()) return false;
  const auto& tags = labels(resource, object);
  if (!tags.is_object() || !tags.contains("org.graphx.owner") ||
      !tags.contains("org.graphx.graph") ||
      tags.at("org.graphx.owner") != Value(resource.process_identity) ||
      tags.at("org.graphx.graph") != Value(graph))
    throw std::runtime_error("E_DOCKER_IDENTITY: owner label mismatch; nothing removed");
  if (resource.kind == "container") {
    if (object.at("Name") != Value("/" + resource.name))
      throw std::runtime_error("E_DOCKER_IDENTITY: container name changed");
    if (object.at("Image") != Value(resource.secondary_id))
      throw std::runtime_error("E_DOCKER_IDENTITY: container image mismatch");
    resource.stable_id = object.at("Id").text();
  } else if (resource.kind == "network") {
    resource.stable_id = object.at("Id").text();
  } else {
    if (resource.secondary_id != "pending" &&
        object.at("CreatedAt") != Value(resource.secondary_id))
      throw std::runtime_error("E_DOCKER_IDENTITY: volume incarnation changed");
    resource.stable_id = object.at("Name").text();
    resource.secondary_id = object.at("CreatedAt").text();
  }
  return true;
}
void write(const std::filesystem::path& path, const std::string& bytes) {
  safe_execution_path(path, true);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << bytes;
  if (!stream) throw std::runtime_error("E_DOCKER_OUTPUT: cannot write runtime projection");
  ::chmod(path.c_str(), 0600);
}
std::string replace(std::string text, const std::map<std::string, std::string>& values) {
  for (const auto& [key, value] : values) {
    std::size_t pos{};
    while ((pos = text.find("${" + key + "}", pos)) != std::string::npos) {
      text.replace(pos, key.size() + 3, value);
      pos += value.size();
    }
  }
  if (text.find("${") != std::string::npos)
    throw std::runtime_error("E_SUBSTITUTION: unresolved Compose field");
  return text;
}
Value project(Value value, const std::map<std::string, std::string>& values) {
  if (value.is_string()) return replace(value.text(), values);
  if (value.is_object())
    for (auto& [key, entry] : value.object()) {
      (void)key;
      entry = project(entry, values);
    }
  if (value.is_array())
    for (auto& entry : value.array()) entry = project(entry, values);
  return value;
}
std::string hex_bytes(std::string_view bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (const unsigned char byte : bytes) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}
}  // namespace
int execute_compose(const ExecutionOptions& opts, const ConfigValue& resolved,
                    std::ostream& output) {
  const auto graph = resolved.at("graph_id").text();
  const auto root = opts.state_root / graph;
  const auto state_file = root / "ownership.yml";
  const auto state_volume = "graphx-" + graph + "-history";
  const auto credential_volume = "graphx-" + graph + "-credentials";
  const auto configuration = configuration_hash(opts.output / "compile-manifest.json");
  const bool has_guests = std::ranges::any_of(resolved.at("nodes").array(), [](const auto& node) {
    return node.at("execution").at("kind") == Value("qemu");
  });
  Value verified_guests = Array{};
  if (opts.action == "up" && has_guests) {
    verified_guests = verify_guest_artifacts(opts, resolved);
    require_guest_account();
  }
  const bool ovs = !resolved.at("network").at("switches").array().empty();
  if (ovs) {
    if (!opts.allow_privileged)
      throw std::runtime_error(
          "E_PRIVILEGED_AUTHORIZATION: OVS requires explicit --allow-privileged on Linux");
#if !defined(__linux__)
    throw std::runtime_error("E_TARGET: OVS requires native Linux or the GraphX Lima guest");
#else
    if (::geteuid() != 0)
      throw std::runtime_error(
          "E_PRIVILEGED_REQUIRED: OVS requires the authorized Linux root runner");
#endif
    if (resolved.at("target") != Value("native-linux") && resolved.at("target") != Value("lima"))
      throw std::runtime_error("E_TARGET: OVS target must be native-linux or lima");
    for (const auto& attachment : resolved.at("network").at("attachments").array())
      if (attachment.at("kind") == Value("external"))
        throw std::runtime_error(
            "E_PHASE_UNAVAILABLE: physical-device uplink ownership contract is not defined");
  }
  if (opts.action == "up") {
    for (const auto& node : resolved.at("nodes").array())
      if (node.at("execution").at("kind") != Value("container") &&
          node.at("execution").at("kind") != Value("external") &&
          node.at("execution").at("kind") != Value("namespace") &&
          node.at("execution").at("kind") != Value("qemu"))
        throw std::runtime_error(
            "E_PHASE_UNAVAILABLE: graph requires native, namespace or guest execution");
    safe_execution_path(root, true);
    safe_execution_path(opts.images);
    require_available_tcp_port(
        static_cast<std::uint16_t>(resolved.at("platform").at("console").at("port").integer()));
    for (const auto& extension : resolved.at("platform").at("extensions").array())
      require_available_tcp_port(extension == Value("prometheus") ? 9090 : 3000);
  } else if (!std::filesystem::exists(state_file)) {
    output << "inactive graph=" << graph << '\n';
    return 0;
  }
#if defined(__APPLE__)
  if (resolved.at("target") != Value("orbstack"))
    throw std::runtime_error("E_TARGET: use the selected OrbStack target on macOS");
  if (call({"docker", "context", "show"}) != "orbstack")
    throw std::runtime_error("E_TARGET: macOS portable execution requires selected OrbStack");
#endif
  if (ovs) {
    const auto* override_host = std::getenv("DOCKER_HOST");
    const auto endpoint =
        override_host && *override_host
            ? std::string(override_host)
            : call({"docker", "context", "inspect", "--format", "{{.Endpoints.docker.Host}}"});
    if (!endpoint.starts_with("unix:///"))
      throw std::runtime_error("E_TARGET: OVS requires a local Unix Docker socket");
    struct stat socket{};
    if (::stat(endpoint.substr(7).c_str(), &socket) != 0 || !S_ISSOCK(socket.st_mode) ||
        socket.st_uid != 0)
      throw std::runtime_error("E_TARGET: OVS requires a root-owned local Docker socket");
  }
  call({"docker", "info", "--format", "{{.OSType}}/{{.Architecture}}"});
  if (opts.action == "up") {
    ensure_state_root(opts.state_root);
    ensure_state_root(root);
  }
  safe_execution_path(root);
  auto lock = [&] {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    for (;;) {
      try {
        return OwnershipLock::open_or_create(root / ".lock", OwnershipLockMode::exclusive);
      } catch (const std::runtime_error& error) {
        if (!ovs || opts.action == "up" || Clock::now() >= deadline ||
            std::string_view(error.what()) !=
                "another infrastructure operation owns the graph lock")
          throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  }();
  OwnershipState state;
  if (std::filesystem::exists(state_file)) {
    state = load_state(state_file);
    if (state.graph_id != graph || state.config_hash != configuration)
      throw std::runtime_error("E_EXECUTION_IDENTITY: state belongs to another compilation");
  } else {
    state.graph_id = graph;
    state.config_hash = configuration;
    state.owner_token = random_token();
    state.status = "creating";
    state.actions.clear();
  }
  const auto save = [&] { save_state(state_file, state); };
  const auto network_action = [&](OvsLifecycleAction action, bool validate_only = false) {
    GraphConfig config;
    config.version = 3;
    config.id = graph;
    config.resolved = resolved;
    config.network_infrastructure = resolved_network(resolved.at("network"));
    config.deployment.project = "graphx-" + graph;
    for (const auto& resource : state.processes)
      if (resource.kind == "container") {
        const auto name = resource.name.substr(("graphx-" + graph + "-").size());
        config.deployment.services.push_back({name, resource.secondary_id, {}});
      }
    OvsExecutionContext context{state, lock, [] { return cancelled != 0; }, validate_only, {}};
    if (available_startup(resolved) && action == OvsLifecycleAction::status)
      for (auto resource : state.processes) {
        if (resource.kind != "container") continue;
        const auto name = resource.name.substr(("graphx-" + graph + "-").size());
        if (!state.applications.contains(name)) continue;
        if (!observe(resource, graph))
          throw std::runtime_error("E_DOCKER_IDENTITY: missing application container");
        const auto object = inspect("container", resource.stable_id);
        if (!object.at("State").at("Running").boolean())
          context.unavailable_containers.insert(name);
      }
    return execute_ovs_lifecycle_impl(config, opts.output / "compile-manifest.json", action, false,
                                      opts.state_root, output, output, &context);
  };
  const auto stop = [&] {
    for (auto& resource : state.processes) {
      if (resource.kind == "native") {
        native_process_status(resource);
        verify_guest_directory(resource);
      } else
        observe(resource, graph);
    }
    save();
    if (ovs && (!state.expected_bridges.empty() || !state.expected_endpoints.empty()))
      network_action(OvsLifecycleAction::destroy, true);
    const auto barrier = root / "barriers/release";
    if (std::filesystem::exists(barrier)) {
      safe_execution_path(barrier);
      if (read_document(barrier, 128) != state.owner_token)
        throw std::runtime_error("E_BARRIER_IDENTITY: refusing cleanup");
      std::filesystem::remove(barrier);
    }
    const auto prepared = root / "barriers/network-ready";
    if (std::filesystem::exists(prepared)) {
      safe_execution_path(prepared);
      if (read_document(prepared, 128) != state.owner_token)
        throw std::runtime_error("E_BARRIER_IDENTITY: refusing network barrier cleanup");
      std::filesystem::remove(prepared);
    }
    if (ovs)
      for (auto& resource : state.processes)
        if (resource.kind == "container" &&
            std::ranges::any_of(resolved.at("nodes").array(),
                                [&](const auto& node) {
                                  return node.at("execution").at("kind") == Value("container") &&
                                         resource.name ==
                                             "graphx-" + graph + "-" + node.at("node_id").text();
                                }) &&
            observe(resource, graph) &&
            inspect("container", resource.stable_id).at("State").at("Running").boolean())
          call({"docker", "stop", "--time", "5", resource.stable_id});
    for (std::size_t index = state.processes.size(); index > 0; --index)
      if (state.processes[index - 1].kind == "native") {
        stop_native_process(state.processes[index - 1]);
        cleanup_guest_directory(state.processes[index - 1]);
        state.processes.erase(state.processes.begin() + static_cast<std::ptrdiff_t>(index - 1));
        save();
      }
    if (ovs && (!state.expected_bridges.empty() || !state.expected_endpoints.empty()))
      network_action(OvsLifecycleAction::destroy);
    state.status = "destroying";
    save();
    for (std::size_t index = state.processes.size(); index > 0; --index) {
      auto& resource = state.processes[index - 1];
      if (resource.kind == "volume" && resource.name != credential_volume &&
          resource.stable_id != "pending")
        continue;
      if (observe(resource, graph)) {
        if (resource.kind == "container") {
          const auto object = inspect(resource.kind, resource.stable_id);
          if (object.at("State").at("Running").boolean())
            call({"docker", "stop", "--time", "5", resource.stable_id});
          write(root / "logs" / (resource.name + ".log"),
                call({"docker", "logs", "--tail", "4096", resource.stable_id}, {}, false));
          call({"docker", "container", "rm", resource.stable_id});
        } else
          call({"docker", resource.kind, "rm", resource.stable_id});
      }
      state.processes.erase(state.processes.begin() + static_cast<std::ptrdiff_t>(index - 1));
      save();
    }
  };
  if (opts.action == "status") {
    const bool network_ready = !ovs || network_action(OvsLifecycleAction::status) == 0;
    std::map<std::string, bool> live;
    bool platform_live = false;
    for (auto resource : state.processes) {
      if (resource.kind == "native") verify_guest_directory(resource);
      const bool present = resource.kind == "native" ? native_process_status(resource) != 0
                                                     : observe(resource, graph);
      output << resource.kind << ' ' << resource.name << ' ' << (present ? "present" : "absent")
             << '\n';
      if (resource.kind == "container" && available_startup(resolved)) {
        const auto name = resource.name.substr(("graphx-" + graph + "-").size());
        const bool running =
            present && inspect("container", resource.stable_id).at("State").at("Running").boolean();
        if (name == "platform") platform_live = running;
        if (state.applications.contains(name)) {
          live[name] = running && state.applications.at(name) == "ready";
          output << "application=" << name << " admission=" << state.applications.at(name)
                 << " status=" << (live[name] ? "ready" : "unavailable") << '\n';
        }
      }
    }
    if (available_startup(resolved))
      output << "graph=" << graph << " status="
             << (platform_live && network_ready ? application_status(resolved, live)
                                                : "unavailable")
             << " evidence=process-liveness throughput=unverified\n";
    return network_ready ? 0 : 2;
  }
  if (opts.action == "down") {
    stop();
    output << "stopped graph=" << graph << "; history retained\n";
    return 0;
  }
  for (const auto& resource : state.processes)
    if (resource.kind != "volume" || resource.name == credential_volume)
      throw std::runtime_error("E_EXECUTION_ACTIVE: use run down before another startup");
  const auto images = read(opts.images / "images.json");
  const auto pins = read(opts.output / "catalog-pins.json");
  if (!pins.at("documents").contains("release-images.json"))
    throw std::runtime_error("E_RELEASE_IDENTITY: compile with the verified image release catalog");
  const auto& evidence = pins.at("documents").at("release-images.json").at("images");
  std::map<std::string, std::string> image_ids;
  for (const auto& [role, record] : images.at("images").object()) {
    const auto& inspection = record.at("inspection");
    const auto pin = "graphx-" + role + "@" + inspection.at("digest").text();
    const auto id = inspection.at("config_digest").text();
    if (evidence.at(role).at("image") != Value(pin) ||
        evidence.at(role).at("config_digest") != Value(id))
      throw std::runtime_error("E_RELEASE_IDENTITY: compiled image provenance mismatch");
    const auto archive = opts.images / (role + ".oci.tar");
    safe_execution_path(archive);
    if (configuration_hash(archive) != record.at("archive_sha256").text())
      throw std::runtime_error("E_RELEASE_IDENTITY: OCI archive hash mismatch");
    image_ids[pin] = id;
  }
  // The verified compilation remains private. Containers receive a separate,
  // read-only copy of its non-secret documents beneath this owned state root.
  const auto container_config = root / ("configuration-" + random_token());
  if (!std::filesystem::create_directory(container_config))
    throw std::runtime_error("E_DOCKER_OUTPUT: runtime configuration collision");
  for (const auto& entry : std::filesystem::recursive_directory_iterator(opts.output)) {
    safe_execution_path(entry.path());
    const auto destination = container_config / entry.path().lexically_relative(opts.output);
    if (entry.is_directory()) {
      std::filesystem::create_directory(destination);
      std::filesystem::permissions(destination, std::filesystem::perms::owner_all |
                                                    std::filesystem::perms::group_read |
                                                    std::filesystem::perms::group_exec |
                                                    std::filesystem::perms::others_read |
                                                    std::filesystem::perms::others_exec);
    } else if (entry.is_regular_file()) {
      std::filesystem::copy_file(entry.path(), destination);
      std::filesystem::permissions(destination, std::filesystem::perms::owner_read |
                                                    std::filesystem::perms::group_read |
                                                    std::filesystem::perms::others_read);
    } else {
      throw std::runtime_error("E_DOCKER_OUTPUT: unexpected compilation entry");
    }
  }
  std::filesystem::permissions(container_config, std::filesystem::perms::owner_all |
                                                     std::filesystem::perms::group_read |
                                                     std::filesystem::perms::group_exec |
                                                     std::filesystem::perms::others_read |
                                                     std::filesystem::perms::others_exec);
  const auto source = read(opts.output / "compose.yaml");
  for (const auto& [name, network] : source.at("networks").object()) {
    (void)name;
    for (const auto& [key, value] : network.object()) {
      (void)value;
      if (key != "driver" && key != "internal" && key != "ipam")
        throw std::runtime_error("E_COMPOSE_SECURITY: unsupported network field");
    }
    if (network.at("driver") != Value("bridge"))
      throw std::runtime_error("E_COMPOSE_SECURITY: portable networks must use bridge");
  }
  for (const auto& [name, service] : source.at("services").object()) {
    const std::set<std::string> allowed{
        "image",       "restart",    "read_only", "user",    "cap_drop",   "security_opt",
        "tmpfs",       "pids_limit", "mem_limit", "logging", "entrypoint", "command",
        "environment", "volumes",    "networks",  "labels",  "ports"};
    for (const auto& [key, value] : service.object()) {
      (void)value;
      if (!allowed.contains(key))
        throw std::runtime_error("E_COMPOSE_SECURITY: unsupported service field");
    }
    if (service.at("cap_drop") != Value(Array{Value("ALL")}) ||
        service.at("security_opt") != Value(Array{Value("no-new-privileges:true")}) ||
        service.at("pids_limit").integer() < 1 || service.at("pids_limit").integer() > 1024 ||
        service.at("mem_limit").integer() < 1048576 ||
        service.at("mem_limit").integer() > 4294967296LL)
      throw std::runtime_error("E_COMPOSE_SECURITY: invalid resource boundary");
    if (name != "platform" && name != "prometheus" && name != "grafana") {
      const auto node = std::ranges::find_if(resolved.at("nodes").array(), [&](const auto& item) {
        return item.at("node_id") == Value(name);
      });
      if (node == resolved.at("nodes").array().end())
        throw std::runtime_error("E_COMPOSE_SECURITY: unknown application");
      const auto types = parse_document(runtime_types);
      const auto type = std::ranges::find_if(
          types.array(), [&](const auto& item) { return item.at("id") == node->at("type"); });
      if (type == types.array().end() ||
          service.at("entrypoint") != Value(Array{type->at("executable")}) ||
          service.at("command") !=
              Value(Array{Value("--node"), Value(name), Value("--config"),
                          Value("/run/graphx/node.json"), Value("--release-file"),
                          Value("/run/graphx/barriers/release"), Value("--release-token"),
                          Value("${GX_OWNER}")}))
        throw std::runtime_error(
            "E_COMPOSE_SECURITY: application argv does not match fixed template");
      if (service.at("networks").contains("mg-console"))
        throw std::runtime_error(
            "E_COMPOSE_SECURITY: applications cannot join the console network");
    }
    const auto pin = service.at("image").text();
    if (!image_ids.contains(pin) && pin.find("@sha256:") == std::string::npos)
      throw std::runtime_error("E_RELEASE_IDENTITY: unpinned service");
    if (service.contains("privileged") || service.contains("build") ||
        !service.at("read_only").boolean() || service.at("user") != Value("65532:65532") ||
        service.at("restart") != Value("no"))
      throw std::runtime_error("E_COMPOSE_SECURITY: unsupported service policy");
  }
  ensure_state_root(root / "logs");
  prepare_node_console(root, state);
  const auto barriers = root / "barriers";
  if (!std::filesystem::exists(barriers)) {
    ensure_state_root(barriers);
    if (::chmod(barriers.c_str(), 0755) != 0)
      throw std::runtime_error("E_BARRIER_IDENTITY: cannot prepare barrier directory");
  } else {
    safe_execution_path(barriers);
    struct stat metadata{};
    if (::lstat(barriers.c_str(), &metadata) != 0 || !S_ISDIR(metadata.st_mode) ||
        metadata.st_uid != ::geteuid() || (metadata.st_mode & 0777) != 0755)
      throw std::runtime_error("E_BARRIER_IDENTITY: unexpected barrier directory identity");
  }
  state.status = "creating";
  state.actions.clear();
  save();
  cancelled = 0;
  const auto old_int = std::signal(SIGINT, cancel);
  const auto old_term = std::signal(SIGTERM, cancel);
  const auto restore = [&] {
    std::signal(SIGINT, old_int);
    std::signal(SIGTERM, old_term);
  };
  const auto check = [&] {
    if (cancelled)
      throw std::runtime_error("E_INTERRUPTED: Compose startup interrupted before release");
  };
  const auto own = [&](std::string kind, std::string name, std::string expected) -> std::size_t {
    if (!inspect(kind, name).is_null())
      throw std::runtime_error("E_DOCKER_COLLISION: existing resource " + name);
    OwnedResourceIdentity resource;
    resource.kind = std::move(kind);
    resource.name = std::move(name);
    resource.stable_id = "pending";
    resource.secondary_id = std::move(expected);
    resource.process_identity = state.owner_token;
    state.processes.push_back(resource);
    save();
    return state.processes.size() - 1;
  };
  try {
    for (const auto& [role, record] : images.at("images").object()) {
      check();
      auto id = record.at("inspection").at("config_digest").text();
      if (call({"docker", "image", "inspect", id}, {}, false).empty())
        call({"docker", "image", "load", "--input", (opts.images / (role + ".oci.tar")).string()},
             {}, true, 120000);
      if (call({"docker", "image", "inspect", id}, {}, false).empty())
        id = record.at("inspection").at("digest").text();
      const auto actual = parse_document(call({"docker", "image", "inspect", id})).array().front();
      if (actual.at("Id") != Value(id))
        throw std::runtime_error("E_RELEASE_IDENTITY: loaded image mismatch");
      image_ids["graphx-" + role + "@" + record.at("inspection").at("digest").text()] = id;
    }
    if (ovs && !resolved.at("network").at("captures").array().empty()) {
      state.handoff_name = "handoff-" + random_token();
      state.handoff_inode = 0;
      state.handoff_device = 0;
      save();
      const auto handoff = root / state.handoff_name;
      if (std::filesystem::exists(handoff))
        throw std::runtime_error("E_CAPTURE_HANDOFF: directory collision");
      ensure_state_root(handoff);
      struct stat metadata{};
      if (::chmod(handoff.c_str(), 0755) != 0 || ::lstat(handoff.c_str(), &metadata) != 0)
        throw std::runtime_error("E_CAPTURE_HANDOFF: cannot prepare directory");
      state.handoff_inode = static_cast<std::uint64_t>(metadata.st_ino);
      state.handoff_device = static_cast<std::uint64_t>(metadata.st_dev);
      save();
    }
    std::vector<std::string> volume_names{state_volume, credential_volume};
    for (const auto& node : resolved.at("nodes").array())
      if (node.at("capture").at("enabled").boolean() &&
          node.at("capture").at("provider") == Value("application"))
        volume_names.push_back("graphx-" + graph + "-capture-" + node.at("node_id").text());
    for (const auto& name : volume_names) {
      auto existing = std::ranges::find_if(state.processes, [&](const auto& item) {
        return item.kind == "volume" && item.name == name;
      });
      if (existing != state.processes.end()) {
        if (!observe(*existing, graph))
          throw std::runtime_error("E_DOCKER_IDENTITY: retained history volume disappeared");
        continue;
      }
      const auto index = own("volume", name, "pending");
      call({"docker", "volume", "create", "--label", "org.graphx.owner=" + state.owner_token,
            "--label", "org.graphx.graph=" + graph, name});
      observe(state.processes[index], graph);
      save();
    }
    const auto telemetry =
        image_ids.at("graphx-telemetry@" +
                     images.at("images").at("telemetry").at("inspection").at("digest").text());
    const auto staging_name = "graphx-" + graph + "-stage";
    const auto stage_index = own("container", staging_name, telemetry);
    Object external;
    const auto credentials = read(opts.output / "credentials.json");
    for (const auto& [ref, entry] : credentials.at("entries").object()) {
      if (entry.at("provider") != Value("external")) continue;
      for (const auto& member : entry.at("members").array()) {
        const auto path = opts.external_credentials / ref / member.text();
        safe_execution_path(path);
        struct stat metadata{};
        if (::lstat(path.c_str(), &metadata) != 0 || (metadata.st_mode & 077) != 0)
          throw std::runtime_error("E_CREDENTIAL: external file is not private");
        external[ref][member.text()] = hex_bytes(read_document(path, 65536));
      }
    }
    const std::string stage_script = R"js(
import {mkdirSync,writeFileSync,readFileSync,rmSync} from 'node:fs';
import {stageCredentials,readJson} from '/app/credentials.mjs';
const request=JSON.parse(readFileSync(0,'utf8'));
const files=request.external;
mkdirSync('/var/lib/graphx/external',{mode:0o700});
for(const [ref,members] of Object.entries(files)) {
 mkdirSync('/var/lib/graphx/external/'+ref,{mode:0o700});
 for(const [name,value] of Object.entries(members)) writeFileSync('/var/lib/graphx/external/'+ref+'/'+name,Buffer.from(value,'hex'),{mode:0o400});
}
stageCredentials(readJson('/run/graphx/credentials.json'),'/var/lib/graphx/credentials',{externalRoot:'/var/lib/graphx/external'});
rmSync('/var/lib/graphx/external',{recursive:true});
const guests={};
for(const [node,refs] of Object.entries(request.guests)) {
 guests[node]={};
 for(const [name,ref] of Object.entries(refs)) {
  guests[node][ref]={};
  for(const member of [...readJson('/run/graphx/credentials.json').entries[ref].members,'generation.json']) {
   const bytes=readFileSync('/var/lib/graphx/credentials/'+ref+'/'+member);
   if(bytes.length>65536) throw Error('guest credential exceeds bounds');
   guests[node][ref][member]=bytes.toString('hex');
  }
 }
}
process.stdout.write(JSON.stringify(guests));
)js";
    call({"docker",
          "create",
          "--name",
          staging_name,
          "--log-driver",
          "none",
          "--interactive",
          "--label",
          "org.graphx.owner=" + state.owner_token,
          "--label",
          "org.graphx.graph=" + graph,
          "--read-only",
          "--user",
          "65532:65532",
          "--cap-drop",
          "ALL",
          "--security-opt",
          "no-new-privileges:true",
          "--network",
          "none",
          "--pids-limit",
          "64",
          "--memory",
          "512m",
          "--tmpfs",
          "/tmp:rw,nosuid,nodev,size=16m",
          "--mount",
          "type=bind,src=" + container_config.string() + ",dst=/run/graphx,readonly",
          "--mount",
          "type=volume,src=" + credential_volume + ",dst=/var/lib/graphx",
          telemetry,
          "node",
          "--input-type=module",
          "-e",
          stage_script});
    observe(state.processes[stage_index], graph);
    save();
    Object guest_refs;
    for (const auto& node : resolved.at("nodes").array())
      if (node.at("execution").at("kind") == Value("qemu"))
        guest_refs[node.at("node_id").text()] = node.at("credentials");
    const auto guest_credentials = parse_document(call(
        {"docker", "start", "--attach", "--interactive", state.processes[stage_index].stable_id},
        config_value_json(Object{{"external", external}, {"guests", guest_refs}}), true, 60000));
    const auto staged = inspect("container", state.processes[stage_index].stable_id);
    if (staged.at("State").at("ExitCode") != Value(0))
      throw std::runtime_error("E_CREDENTIAL: staging failed");
    call({"docker", "rm", state.processes[stage_index].stable_id});
    state.processes.erase(state.processes.begin() + static_cast<std::ptrdiff_t>(stage_index));
    save();
    auto compose = source;
    Object volumes;
    volumes[credential_volume] = Object{{"external", true}, {"name", credential_volume}};
    for (const auto& name : volume_names)
      volumes[name] = Object{{"external", true}, {"name", name}};
    compose["volumes"] = volumes;
    const std::map<std::string, std::string> values{{"GX_OUTPUT", container_config.string()},
                                                    {"GX_STATE", root.string()},
                                                    {"GX_OWNER", state.owner_token},
                                                    {"GX_CREDENTIALS", "/unused-credential-path"},
                                                    {"GX_RELEASE", opts.release.string()}};
    for (auto& [name, network] : compose["networks"].object()) {
      const auto actual = "graphx-" + graph + "-" + name;
      network["name"] = actual;
      network["labels"] =
          Object{{"org.graphx.owner", state.owner_token}, {"org.graphx.graph", graph}};
      own("network", actual, state.owner_token);
    }
    std::vector<std::string> applications;
    for (auto& [name, service] : compose["services"].object()) {
      const auto pin = service.at("image").text();
      if (image_ids.contains(pin))
        service["image"] = image_ids.at(pin);
      else {
        if (call({"docker", "image", "inspect", pin}, {}, false).empty())
          call({"docker", "pull", pin}, {}, true, 120000);
        service["image"] =
            parse_document(call({"docker", "image", "inspect", pin})).array().front().at("Id");
      }
      const auto actual = "graphx-" + graph + "-" + name;
      service["container_name"] = actual;
      service["labels"]["org.graphx.owner"] = state.owner_token;
      service["labels"]["org.graphx.graph"] = graph;
      Array mounts;
      for (const auto& mount : service.at("volumes").array()) {
        const auto& text = mount.text();
        const auto separator = text.find(':');
        const auto source_path = text.substr(0, separator);
        const auto end = text.find(':', separator + 1);
        const auto target = text.substr(separator + 1, end - separator - 1);
        if (source_path.starts_with("${GX_CREDENTIALS}/")) {
          const auto ref = source_path.substr(std::string("${GX_CREDENTIALS}/").size());
          mounts.emplace_back(Object{{"type", "volume"},
                                     {"source", credential_volume},
                                     {"target", target},
                                     {"read_only", true},
                                     {"volume", Object{{"subpath", "credentials/" + ref}}}});
        } else if (target == "/run/graphx/console" && name == "platform") {
          mounts.emplace_back(Object{{"type", "bind"},
                                     {"source", (root / state.console_name).string()},
                                     {"target", target},
                                     {"read_only", true}});
        } else if (target == "/captures" && name == "platform") {
          if (ovs && !state.handoff_name.empty())
            mounts.emplace_back(Object{{"type", "bind"},
                                       {"source", (root / state.handoff_name).string()},
                                       {"target", "/captures"},
                                       {"read_only", true}});
          continue;
        } else if (target == "/captures" && name != "platform") {
          mounts.emplace_back(Object{{"type", "volume"},
                                     {"source", "graphx-" + graph + "-capture-" + name},
                                     {"target", "/captures"}});
        } else if (target == "/var/lib/graphx/history") {
          mounts.emplace_back(
              Object{{"type", "volume"}, {"source", state_volume}, {"target", "/var/lib/graphx"}});
        } else {
          auto path = source_path.starts_with("./")
                          ? container_config / source_path.substr(2)
                          : std::filesystem::path(replace(source_path, values));
          path = path.lexically_normal();
          if (path != root / "barriers" &&
              !path.string().starts_with(container_config.string() + "/"))
            throw std::runtime_error(
                "E_COMPOSE_SECURITY: bind is outside the compilation and barrier roots");
          safe_execution_path(path);
          mounts.emplace_back(Object{{"type", "bind"},
                                     {"source", path.string()},
                                     {"target", target},
                                     {"read_only", true}});
        }
      }
      if (name == "platform")
        for (const auto& node : resolved.at("nodes").array())
          if (node.at("capture").at("enabled").boolean() &&
              node.at("capture").at("provider") == Value("application")) {
            const auto capture_id = node.at("node_id").text();
            mounts.emplace_back(Object{{"type", "volume"},
                                       {"source", "graphx-" + graph + "-capture-" + capture_id},
                                       {"target", "/captures/" + capture_id},
                                       {"read_only", true}});
          }
      service["volumes"] = mounts;
      own("container", actual, service.at("image").text());
      if (name != "platform" && name != "prometheus" && name != "grafana")
        applications.push_back(name);
      if (ovs && name != "platform" && name != "prometheus" && name != "grafana") {
        auto argv = service.at("entrypoint").array();
        argv.insert(argv.end(), service.at("command").array().begin(),
                    service.at("command").array().end());
        Array command{Value("-c"),
                      Value("trap 'exit 130' INT TERM; i=0; while [ \"$$i\" -lt 600 ]; do "
                            "if [ -f /run/graphx/barriers/network-ready ] && "
                            "[ \"$$(cat /run/graphx/barriers/network-ready)\" = \"$$1\" ]; then "
                            "shift; exec \"$$@\"; fi; "
                            "i=$$((i+1)); sleep 0.1; done; exit 124"),
                      Value("graphx-network-prepare"), Value(state.owner_token)};
        command.insert(command.end(), argv.begin(), argv.end());
        service["entrypoint"] = Array{Value("/bin/sh")};
        service["command"] = command;
      }
    }
    compose = project(compose, values);
    const auto projection = root / "compose.json";
    write(projection, config_value_json(compose));
    const auto compose_call = [&](std::vector<std::string> arguments) {
      std::vector<std::string> argv{
          "docker",          "compose", "--project-directory", opts.output.string(), "-p",
          "graphx-" + graph, "-f",      projection.string()};
      argv.insert(argv.end(), arguments.begin(), arguments.end());
      return call(argv, {}, true, 60000);
    };
    check();
    compose_call({"create", "--no-build", "--pull", "never"});
    for (auto& resource : state.processes) observe(resource, graph);
    save();
    compose_call({"start", "platform"});
    const auto platform_name = "graphx-" + graph + "-platform";
    auto platform_resource = *std::ranges::find_if(
        state.processes, [&](const auto& item) { return item.name == platform_name; });
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (true) {
      check();
      if (Clock::now() >= deadline) throw std::runtime_error("E_READINESS_TIMEOUT: platform");
      const auto object = inspect("container", platform_resource.stable_id);
      if (!object.at("State").at("Running").boolean())
        throw std::runtime_error("E_READINESS_EXIT: platform");
      const auto port = std::to_string(resolved.at("platform").at("console").at("port").integer());
      const auto ready = call(
          {"docker", "exec", platform_resource.stable_id, "node", "--input-type=module", "-e",
           "const r=await fetch('http://127.0.0.1:" + port +
               "/api/ready',{signal:AbortSignal.timeout(500)}); if(r.ok) console.log('ready')"},
          {}, false, 3000);
      if (ready == "ready") break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (const auto& extension : resolved.at("platform").at("extensions").array()) {
      compose_call({"start", extension.text()});
      const bool prometheus = extension == Value("prometheus");
      const auto url =
          prometheus ? "http://prometheus:9090/api/v1/targets" : "http://grafana:3000/api/health";
      const auto condition =
          prometheus ? "j.data.activeTargets.some(t=>t.health==='up')" : "j.database==='ok'";
      const auto script =
          std::string("const r=await fetch('") + url +
          "',{signal:AbortSignal.timeout(500)}); const j=await r.json(); if(r.ok&&(" + condition +
          ")) console.log('ready')";
      const auto extension_deadline = Clock::now() + std::chrono::seconds(45);
      while (true) {
        check();
        if (Clock::now() >= extension_deadline)
          throw std::runtime_error("E_READINESS_TIMEOUT: " + extension.text());
        if (call({"docker", "exec", platform_resource.stable_id, "node", "--input-type=module",
                  "-e", script},
                 {}, false, 3000) == "ready")
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    if (!applications.empty()) {
      auto start = std::vector<std::string>{"start"};
      start.insert(start.end(), applications.begin(), applications.end());
      compose_call(start);
    }
    if (ovs) {
      check();
      network_action(OvsLifecycleAction::create);
      check();
      publish_execution_file(root / "barriers/network-ready", state.owner_token, 0444);
      for (const auto& node : resolved.at("nodes").array()) {
        if (node.at("execution").at("kind") != Value("namespace")) continue;
        const auto name = node.at("node_id").text();
        const auto ns_name =
            resource_name(graph, "namespace", string_or(node.at("execution"), "namespace", name));
        const auto owned =
            std::ranges::find(state.namespaces, ns_name, &OwnedResourceIdentity::name);
        if (owned == state.namespaces.end() || !namespace_owned(*owned, state))
          throw std::runtime_error("E_NAMESPACE_IDENTITY: diagnostic namespace not owned");
        NativeProcessOptions start;
        start.id = name;
        start.executable = opts.release / "bin/graphx-diagnostic";
        start.cwd = opts.output;
        start.log = root / "logs" / (name + ".log");
        if (std::filesystem::exists(start.log)) {
          safe_execution_path(start.log);
          safe_execution_path(start.log.string() + ".previous", true);
          std::filesystem::rename(start.log, start.log.string() + ".previous");
        }
        start.network_namespace = "/run/netns/" + ns_name;
        start.namespace_inode = *owned->namespace_inode;
        start.argv = {start.executable.string(),
                      "--node",
                      name,
                      "--config",
                      (opts.output / "nodes" / (name + ".json")).string(),
                      "--release-file",
                      (root / "barriers/release").string(),
                      "--release-token",
                      state.owner_token};
        start_native_process(start, [&](const auto& process) {
          state.processes.push_back(process);
          save();
        });
      }
    }
    std::vector<std::unique_ptr<GuestSession>> guest_sessions;
    if (has_guests) {
      const auto guest_deadline = Clock::now() + std::chrono::seconds(120);
      for (const auto& guest : verified_guests.array()) {
        const auto name = guest.at("node").text();
        const auto& node =
            *std::ranges::find(resolved.at("nodes").array(), Value(name),
                               [](const auto& value) { return value.at("node_id"); });
        guest_sessions.push_back(std::make_unique<GuestSession>(
            opts, guest, node, guest_credentials.at(name),
            root / ("guest-" + name + "-" + random_token()), state.owner_token, guest_deadline,
            [&](const auto& process) {
              state.processes.push_back(process);
              save();
            },
            [] { return cancelled != 0; }));
        struct stat serial{};
        const auto socket = root / state.console_name / (name + ".sock");
        if (::lstat(socket.c_str(), &serial) || !S_ISSOCK(serial.st_mode) || serial.st_uid != 65532)
          throw std::runtime_error("E_CONSOLE_IDENTITY: guest serial socket unavailable");
        state.console_sockets[name] =
            std::to_string(serial.st_dev) + ":" + std::to_string(serial.st_ino);
        save();
      }
    }
    state.applications.clear();
    const auto application_deadline =
        Clock::now() + std::chrono::milliseconds(application_readiness_ms(resolved));
    bool pending = true;
    while (pending) {
      pending = false;
      for (const auto& name : applications) {
        if (state.applications.contains(name)) continue;
        auto resource = *std::ranges::find_if(state.processes, [&](const auto& item) {
          return item.name == "graphx-" + graph + "-" + name;
        });
        check();
        if (!observe(resource, graph))
          throw std::runtime_error("E_DOCKER_IDENTITY: missing application container");
        const auto object = inspect("container", resource.stable_id);
        if (!object.at("State").at("Running").boolean()) {
          if (!available_startup(resolved) ||
              !unavailable_exit(object.at("State").at("ExitCode").integer(), true))
            throw std::runtime_error("E_READINESS_EXIT: " + name);
          state.applications[name] = "exited";
        } else if (Clock::now() >= application_deadline) {
          if (!available_startup(resolved))
            throw std::runtime_error("E_READINESS_TIMEOUT: " + name);
          state.applications[name] = "timeout";
        } else {
          const auto log = call({"docker", "logs", "--tail", "4096", resource.stable_id});
          if (Clock::now() < application_deadline && application_ready(log, name))
            state.applications[name] = "ready";
          else
            pending = true;
        }
      }
      save();
      if (pending) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (auto resource : state.processes) {
      if (resource.kind != "container") continue;
      const auto name = resource.name.substr(("graphx-" + graph + "-").size());
      if (state.applications.contains(name) && state.applications.at(name) == "timeout") {
        if (!observe(resource, graph))
          throw std::runtime_error("E_DOCKER_IDENTITY: missing timed-out container");
        call({"docker", "stop", "--time", "2", resource.stable_id});
      }
    }
    for (const auto& process : state.processes) {
      if (process.kind != "native") continue;
      if (std::ranges::any_of(resolved.at("nodes").array(), [&](const auto& node) {
            return node.at("node_id") == Value(process.name) &&
                   node.at("execution").at("kind") == Value("qemu");
          }))
        continue;
      while (true) {
        check();
        if (Clock::now() >= application_deadline || !native_process_status(process))
          throw std::runtime_error("E_READINESS_TIMEOUT: namespace diagnostic");
        if (read_process_log(root / "logs" / (process.name + ".log"), 2097152)
                .find("ready node=" + process.name) != std::string::npos)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
    check();
    for (auto& resource : state.processes)
      if (resource.kind == "container") {
        if (!observe(resource, graph))
          throw std::runtime_error("E_DOCKER_IDENTITY: missing container before release");
        const auto object = inspect("container", resource.stable_id);
        if (!object.at("State").at("Running").boolean()) {
          const auto name = resource.name.substr(("graphx-" + graph + "-").size());
          if (!available_startup(resolved) || !state.applications.contains(name) ||
              !unavailable_exit(object.at("State").at("ExitCode").integer(), true))
            throw std::runtime_error("E_READINESS_EXIT: process exited before release");
          if (state.applications.at(name) == "ready") state.applications[name] = "exited";
        }
      }
    save();
    if (ovs && network_action(OvsLifecycleAction::status) != 0)
      throw std::runtime_error("E_NETWORK_READINESS: owned data plane is not ready");
    for (auto& guest : guest_sessions) guest->release();
    publish_execution_file(root / "barriers/release", state.owner_token, 0444);
    state.status = "ready";
    save();
    if (ovs && !state.captures.empty()) {
#if defined(__linux__)
      NativeProcessOptions handoff;
      handoff.id = "mg-capture-handoff";
      handoff.executable = std::filesystem::read_symlink("/proc/self/exe");
      handoff.cwd = opts.output;
      handoff.log = root / "logs" / (state.handoff_name + ".log");
      handoff.environment = {{"PATH", "/usr/sbin:/usr/bin:/sbin:/bin"}};
      handoff.argv = {handoff.executable.string(),
                      "run",
                      "handoff",
                      "--output",
                      opts.output.string(),
                      "--state-root",
                      opts.state_root.string(),
                      "--owner",
                      state.owner_token};
      start_native_process(handoff, [&](const auto& process) {
        state.processes.push_back(process);
        save();
      });
#endif
    }
    start_node_console(opts, state);
    restore();
    std::map<std::string, bool> live;
    for (const auto& [name, admission] : state.applications) live[name] = admission == "ready";
    output << (available_startup(resolved) ? application_status(resolved, live) : "ready")
           << " graph=" << graph << " owner=" << state.owner_token << '\n';
    return 0;
  } catch (...) {
    cancelled = 0;
    try {
      stop();
    } catch (const std::exception& error) {
      std::cerr << error.what() << "; recovery state retained\n";
    }
    restore();
    throw;
  }
}
}  // namespace graphx::infra::detail
