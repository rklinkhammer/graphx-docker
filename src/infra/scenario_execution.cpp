#include "infra/scenario_execution.hpp"
#include "config_document.hpp"
#include "graphx/infra.hpp"
#include "graphx/config_schemas.hpp"
#include "infra/ownership_lock.hpp"
#include "infra/ownership_state.hpp"
#include "infra/process_resources.hpp"
#include "infra/namespace_resources.hpp"
#include "infra/endpoint_resources.hpp"
#include "infra/fault_resources.hpp"
#include "infra/qemu_resources.hpp"
#include "infra/lifecycle_coordinator.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <regex>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace graphx::infra::detail {
namespace {
using namespace config_internal;
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }
void require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error("E_SCENARIO: " + message);
}
void checkpoint() { require(!interrupted, "interrupted; action recovery state retained"); }
Value read(const std::filesystem::path& path) {
  safe_execution_path(path);
  return parse_document(read_document(path, 4 * 1024 * 1024));
}
std::string call(std::vector<std::string> arguments, std::string input = {}) {
  CommandOptions options;
  options.arguments = std::move(arguments);
  options.standard_input = std::move(input);
  options.capture_output = true;
  options.output_limit = 1024 * 1024;
  options.timeout_ms = 10000;
  options.cancellation_requested = [] { return interrupted != 0; };
  const auto result = run_command(options);
  require(!result.status && !result.exec_error && !result.output_truncated,
          "bounded command failed: " + options.arguments.front());
  return result.output;
}
Value docker_owned(const OwnedResourceIdentity& process, const OwnershipState& state) {
  const auto objects = parse_document(call({"docker", process.kind, "inspect", process.stable_id}));
  require(objects.array().size() == 1, "ambiguous Docker identity");
  const auto object = objects.array().front();
  const auto& labels =
      process.kind == "container" ? object.at("Config").at("Labels") : object.at("Labels");
  require(labels.at("org.graphx.owner") == Value(state.owner_token) &&
              labels.at("org.graphx.graph") == Value(state.graph_id),
          "Docker owner changed");
  if (process.kind == "container")
    require(object.at("Id") == Value(process.stable_id) &&
                object.at("Image") == Value(process.secondary_id) &&
                object.at("State").at("Running") == Value(true),
            "container identity changed or stopped");
  return object;
}
std::string directory_identity(const std::filesystem::path& path) {
  safe_execution_path(path);
  struct stat metadata{};
  require(::lstat(path.c_str(), &metadata) == 0 && S_ISDIR(metadata.st_mode),
          "directory unavailable");
  return std::to_string(metadata.st_dev) + ":" + std::to_string(metadata.st_ino);
}
const OwnedResourceIdentity& named_process(const OwnershipState& state, const std::string& name) {
  const auto item = std::ranges::find(state.processes, name, &OwnedResourceIdentity::name);
  require(item != state.processes.end(), "process not owned: " + name);
  return *item;
}
const Value& connection(const Value& resolved, const Value& id) {
  const auto item = std::ranges::find(resolved.at("connections").array(), id,
                                      [](const auto& value) { return value.at("id"); });
  require(item != resolved.at("connections").array().end(), "unknown connection");
  return *item;
}
void traffic(const ExecutionOptions& opts, const Value& resolved, const Value& action,
             OwnershipState& state, std::ostream& output) {
  const auto root = opts.state_root / state.graph_id;
  const bool guest =
      std::ranges::find(action.at("expect").array(), Value("bidirectional-tcp-udp")) !=
      action.at("expect").array().end();
  if (guest) {
    const auto process = std::ranges::find_if(
        state.processes, [](const auto& value) { return !value.runtime_directory.empty(); });
    require(process != state.processes.end(), "guest process missing");
    std::string serial;
    if (!state.console_name.empty()) {
      const auto retained = root / state.console_name / (process->name + ".boot");
      if (std::filesystem::exists(retained)) {
        safe_execution_path(retained);
        serial = read_document(retained, 65536);
      }
    }
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < deadline) {
      checkpoint();
      serial += inspect_guest(*process, false).text();
      require(serial.size() <= 2 * 1024 * 1024, "guest evidence bound exceeded");
      if (serial.find(" tcp bytes=") != std::string::npos &&
          serial.find(" udp bytes=") != std::string::npos)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    require(serial.find(" tcp bytes=") != std::string::npos &&
                serial.find(" udp bytes=") != std::string::npos,
            "guest traffic missing");
    for (const auto& id : action.at("connections").array()) {
      const auto& edge = connection(resolved, id);
      const auto node = edge.at("to").at("node").text();
      if (node == process->name) continue;
      require(native_process_status(named_process(state, node)) != 0, "peer process stopped");
      const auto log = read_document(root / "logs" / (node + ".log"), 2 * 1024 * 1024);
      require(log.find(" " + edge.at("transport").text() + " bytes=") != std::string::npos,
              "peer traffic missing");
    }
    for (const auto& expectation : action.at("expect").array()) {
      if (expectation == Value("qmp-pause-resume"))
        (void)inspect_guest(*process, true);
      else if (expectation == Value("capture-nonempty")) {
        require(!state.captures.empty(), "capture missing");
        bool found{};
        for (const auto& capture : state.captures)
          for (const auto& file : std::filesystem::directory_iterator(capture.session_directory))
            if (file.is_regular_file() && file.path().extension() == ".pcapng" &&
                file.file_size() > 64)
              found = true;
        require(found, "capture has no packet evidence");
      } else if (expectation == Value("vlan-43-isolated")) {
        const auto data = std::ranges::find_if(state.expected_endpoints, [&](const auto& value) {
          return value.owner == process->name && value.kind == AttachmentKind::qemu_tap;
        });
        require(data != state.expected_endpoints.end() && data->vlan.access_tag,
                "guest VLAN unavailable");
        const auto isolated =
            std::ranges::find_if(state.expected_endpoints, [&](const auto& value) {
              return value.kind == AttachmentKind::namespace_veth &&
                     value.network_switch == data->network_switch && value.vlan.access_tag &&
                     value.vlan.access_tag != data->vlan.access_tag;
            });
        require(isolated != state.expected_endpoints.end(), "isolated VLAN endpoint unavailable");
        require(call({"ovs-vsctl", "get", "Port", isolated->host_interface, "tag"}) !=
                    call({"ovs-vsctl", "get", "Port", data->host_interface, "tag"}),
                "VLANs are not isolated");
        const auto incoming =
            std::ranges::find_if(resolved.at("connections").array(), [&](const auto& value) {
              return value.at("to").at("node") == Value(process->name) &&
                     value.at("transport") == Value("udp");
            });
        require(incoming != resolved.at("connections").array().end(), "guest UDP input missing");
        (void)inspect_guest(*process, false);
        call({"ip", "netns", "exec", isolated->namespace_name, "python3", "-c",
              "import "
              "socket,sys;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket."
              "SOL_SOCKET,25,sys.argv[1].encode()+b'\\0');s.bind((sys.argv[2],0));s.sendto(b'X'*"
              "127,(sys.argv[3],int(sys.argv[4])))",
              isolated->target_interface, isolated->address.substr(0, isolated->address.find('/')),
              incoming->at("destination_address").text(),
              std::to_string(incoming->at("settings").at("port").integer())});
        std::this_thread::sleep_for(std::chrono::seconds(1));
        checkpoint();
        require(inspect_guest(*process, false).text().find(" udp bytes=127") == std::string::npos,
                "isolated VLAN reached guest");
      } else
        require(expectation == Value("bidirectional-tcp-udp"), "unsupported guest expectation");
    }
    output << "verified guest traffic, VLAN, capture and QMP expectations\n";
    return;
  }
  require(action.at("connections").array().size() == action.at("expect").array().size(),
          "per-connection expectations required");
  for (std::size_t index = 0; index < action.at("connections").array().size(); ++index) {
    const auto& edge = connection(resolved, action.at("connections").array()[index]);
    require(
        edge.at("transport") == Value("udp") && edge.at("schema") == Value("RawDiagnosticDatagram"),
        "traffic needs a declared diagnostic UDP contract");
    const auto& endpoint = expected_endpoint(state, edge.at("attachments").at("from").text());
    require(endpoint.kind == AttachmentKind::namespace_veth,
            "diagnostic sender must be an owned namespace");
    const auto target = edge.at("to").at("node").text();
    require(native_process_status(named_process(state, target)) != 0,
            "diagnostic receiver stopped");
    const auto token = random_token();
    call({"ip", "netns", "exec", endpoint.namespace_name, "python3", "-c",
          "import "
          "socket,sys;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_"
          "SOCKET,25,sys.argv[1].encode()+b'\\0');s.bind((sys.argv[2],0));t=sys.argv[5].encode();s."
          "sendto(b'GXR1\\0'+bytes([len(t)])+t,(sys.argv[3],int(sys.argv[4])))",
          endpoint.target_interface, edge.at("source_address").text(),
          edge.at("destination_address").text(),
          std::to_string(edge.at("settings").at("port").integer()), token});
    bool received{};
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < deadline) {
      checkpoint();
      const auto logpath = root / "logs" / (target + ".log");
      safe_execution_path(logpath);
      received =
          read_document(logpath, 2 * 1024 * 1024).find("token=" + token) != std::string::npos;
      if (received) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const auto expect = action.at("expect").array()[index].text();
    bool pass = expect == "pass";
    if (expect.starts_with("pass-only-after-")) {
      const auto applied =
          std::ranges::find(state.actions, expect.substr(16), &OwnedScenarioAction::id);
      pass = applied != state.actions.end() && applied->status == "complete";
    } else
      require(expect == "pass" || expect == "drop", "unsupported flow expectation");
    require(received == pass, "traffic expectation failed: " + edge.at("id").text());
    output << edge.at("id").text() << "=" << (received ? "pass" : "drop") << '\n';
  }
}
}  // namespace

int execute_scenario(const ExecutionOptions& opts, const ConfigValue& resolved,
                     std::ostream& output) {
  require(opts.action == "scenario-plan" || opts.action == "scenario-run" ||
              opts.action == "scenario-status" || opts.action == "scenario-clear",
          "unknown scenario operation");
  const auto plan = read(opts.output / "scenario-plan.json");
  GraphConfig config;
  config.id = resolved.at("graph_id").text();
  config.version = 3;
  config.resolved = resolved;
  config.network_infrastructure = resolved_network(resolved.at("network"));
  config.deployment.project = "graphx-" + config.id;
  config.authored = Object{{"scenario", Object{{"actions", plan.at("actions")}}},
                           {"credentials", read(opts.output / "credentials.json").at("entries")}};
  const auto schema = parse_document(authored_schema);
  for (const auto& action : plan.at("actions").array())
    validate_shape(action, schema.at("$defs").at("action"), "scenario", &schema);
  require(scenario_plan(config) == plan, "scenario plan contract mismatch");
  const auto action = std::ranges::find(plan.at("actions").array(), Value(opts.scenario_action),
                                        [](const auto& value) { return value.at("id"); });
  if (opts.action == "scenario-plan") {
    if (opts.scenario_action.empty())
      output << config_value_json(plan);
    else {
      require(action != plan.at("actions").array().end(), "unknown action");
      output << config_value_json(*action);
    }
    return 0;
  }
  require(action != plan.at("actions").array().end(), "--action must select a declared action");
  const auto kind = action->at("action").text();
  require(kind != "external-simulator",
          "external-simulator requires explicit compile --laboratory selection");
  if (kind != "credential-rotate") {
#if defined(__linux__)
    require(opts.allow_privileged && ::geteuid() == 0,
            "explicit --allow-privileged and local Linux root required");
#else
    throw std::runtime_error("E_SCENARIO: networking actions require authorized Linux execution");
#endif
  }
  const auto root = opts.state_root / config.id, file = root / "ownership.yml";
  safe_execution_path(file);
  auto lock = OwnershipLock::open_existing(root / ".lock", OwnershipLockMode::exclusive);
  auto state = load_state(file);
  require(state.graph_id == config.id &&
              state.config_hash == configuration_hash(opts.output / "compile-manifest.json") &&
              state.status == "ready",
          "matching ready graph required");
  bind_owned_container_services(config, state);
  interrupted = 0;
  const auto previous_int = std::signal(SIGINT, interrupt),
             previous_term = std::signal(SIGTERM, interrupt);
  struct Restore {
    decltype(previous_int) a, b;
    ~Restore() {
      std::signal(SIGINT, a);
      std::signal(SIGTERM, b);
    }
  } restore{previous_int, previous_term};
  const auto save = [&] { save_state(file, state); };
  auto record = std::ranges::find(state.actions, opts.scenario_action, &OwnedScenarioAction::id);
  if (opts.action == "scenario-status") {
    output << (record == state.actions.end() ? "not-run" : record->status) << '\n';
    return 0;
  }
  require(opts.action != "scenario-clear" || kind == "fault" || kind.starts_with("route-"),
          "clear is only valid for faults and manual routes");
  if (record != state.actions.end())
    require(record->status != "pending", "incomplete action; inspect recovery state before retry");
  if (kind != "credential-rotate") {
    OvsExecutionContext context{state, lock, [] { return interrupted != 0; }, true, {}};
    require(execute_ovs_lifecycle_impl(config, opts.output / "compile-manifest.json",
                                       OvsLifecycleAction::destroy, false, opts.state_root, output,
                                       output, &context) == 0,
            "network ownership check failed");
  }
  checkpoint();
  if (kind == "fault" && opts.action == "scenario-clear") {
    const auto fault = std::ranges::find(state.faults, opts.scenario_action, &OwnedFault::id);
    require(record != state.actions.end() && record->status == "complete" &&
                fault != state.faults.end(),
            "fault not owned");
    require(clear_fault(*fault), "fault identity changed");
    state.faults.erase(fault);
    std::erase_if(state.expected_faults,
                  [&](const auto& value) { return value.definition.id == opts.scenario_action; });
    record->status = "cleared";
    record->identity.clear();
    save();
    output << "fault cleared\n";
    return 0;
  }
  if (kind.starts_with("route-")) {
    const auto& router = config.network_infrastructure.router(action->at("router").text());
    const auto ns =
        std::ranges::find(state.namespaces, router.namespace_name, &OwnedResourceIdentity::name);
    require(ns != state.namespaces.end() && namespace_owned(*ns, state),
            "router namespace identity changed");
    const auto destination = action->at("destination").text();
    const auto current = call({"ip", "netns", "exec", router.namespace_name, "ip", "-j", "route",
                               "show", "exact", destination});
    const bool clear = kind == "route-clear" || opts.action == "scenario-clear";
    if (clear) {
      const auto apply = std::ranges::find_if(plan.at("actions").array(), [&](const auto& value) {
        return value.at("action") == Value("route-apply") &&
               value.at("router") == action->at("router") &&
               value.at("destination") == action->at("destination");
      });
      require(apply != plan.at("actions").array().end(), "route has no apply owner");
      auto owned =
          std::ranges::find(state.actions, apply->at("id").text(), &OwnedScenarioAction::id);
      require(
          owned != state.actions.end() && owned->status == "complete" && owned->identity == current,
          "route identity changed or not owned");
      owned->status = "pending";
      save();
      call(route_command(config, action->at("router").text(), destination, true).arguments);
      owned->status = "cleared";
      owned->identity.clear();
      if (kind == "route-clear") {
        const auto cleared =
            std::ranges::find(state.actions, opts.scenario_action, &OwnedScenarioAction::id);
        if (cleared == state.actions.end())
          state.actions.push_back({opts.scenario_action, kind, "complete", ""});
        else {
          cleared->status = "complete";
          cleared->identity.clear();
        }
      }
      save();
      output << "route cleared\n";
      return 0;
    }
    require(parse_document(current).array().empty(), "refusing pre-existing route");
  }
  if (record == state.actions.end()) {
    state.actions.push_back({opts.scenario_action, kind, "pending", ""});
    record = std::prev(state.actions.end());
  } else {
    require(record->status == "cleared" || kind == "traffic", "action already completed");
    record->status = "pending";
    record->identity.clear();
  }
  save();
  checkpoint();
  if (kind == "route-apply") {
    auto command =
        route_command(config, action->at("router").text(), action->at("destination").text(), false);
    for (auto& value : command.arguments)
      if (value == "replace") value = "add";
    call(command.arguments);
    const auto& router = config.network_infrastructure.router(action->at("router").text());
    record->identity = call({"ip", "netns", "exec", router.namespace_name, "ip", "-j", "route",
                             "show", "exact", action->at("destination").text()});
  } else if (kind == "fault") {
    const auto& endpoint = expected_endpoint(state, action->at("attachment").text());
    ExpectedFault expected;
    expected.interface = endpoint.host_interface;
    auto& fault = expected.definition;
    fault.id = opts.scenario_action;
    fault.attachment = action->at("attachment").text();
    fault.duration_seconds = static_cast<std::uint32_t>(action->at("duration_seconds").integer());
    fault.delay_ms = static_cast<std::uint32_t>(integer_or(*action, "delay_ms", 0));
    fault.jitter_ms = static_cast<std::uint32_t>(integer_or(*action, "jitter_ms", 0));
    fault.loss_percent = action->contains("loss_percent")
                             ? std::stod(config_value_json(action->at("loss_percent"), false))
                             : 0;
    require(std::ranges::none_of(
                state.faults,
                [&](const auto& value) { return value.attachment_id == fault.attachment; }),
            "attachment already has an owned fault");
    state.expected_faults.push_back(expected);
    save();
    (void)create_fault(expected, [&](const auto& owned) {
      state.faults.push_back(owned);
      record->identity = owned.qdisc_identity;
      save();
    });
  } else if (kind == "traffic")
    traffic(opts, resolved, *action, state, output);
  else if (kind == "credential-rotate") {
    const auto manifest = opts.output / "credentials.json";
    const std::string script =
        "import {pathToFileURL} from 'node:url';import {readFileSync} from 'node:fs';const "
        "[module,manifest,root,ref,next,grace]=process.argv.slice(1);const "
        "{rotateCredential,readJson}=await "
        "import(pathToFileURL(module));rotateCredential(manifest==='-'?"
        "JSON.parse(readFileSync(0,'utf8')):readJson(manifest),root,ref,next,Number("
        "grace));console.log('credential generation published');";
    const bool native = resolved.at("platform").at("telemetry").at("host") == Value("127.0.0.1");
    if (native) {
      require(!opts.release.empty() && !state.credential_directory.empty(),
              "verified release and owned credential directory required");
      require(directory_identity(state.credential_directory) == state.credential_identity,
              "credential root replaced");
      require(native_process_status(named_process(state, "platform")) != 0, "platform stopped");
      auto credential_lock =
          OwnershipLock::open_or_create(std::filesystem::path(state.credential_directory) / ".lock",
                                        OwnershipLockMode::exclusive);
      call({(opts.release / "libexec/graphx-platform/node").string(), "--input-type=module", "-e",
            script,
            (opts.release / "libexec/graphx-platform/apps/telemetry/credentials.mjs").string(),
            manifest.string(), state.credential_directory, action->at("credential").text(),
            action->at("next").text(), std::to_string(action->at("grace_seconds").integer())});
    } else {
      const auto platform = named_process(state, "graphx-" + state.graph_id + "-platform");
      (void)docker_owned(platform, state);
      const auto volume = named_process(state, "graphx-" + state.graph_id + "-credentials");
      (void)docker_owned(volume, state);
      const auto name = "graphx-" + state.graph_id + "-scenario-" + random_token();
      state.processes.push_back({.kind = "container",
                                 .name = name,
                                 .stable_id = "pending",
                                 .secondary_id = platform.secondary_id,
                                 .process_identity = state.owner_token});
      save();
      // Fixed tooling image, no network, no secret output, and the common graph lock is held.
      const auto image = state.processes.back().secondary_id;
      auto created = call({"docker",
                           "create",
                           "--interactive",
                           "--name",
                           name,
                           "--label",
                           "org.graphx.owner=" + state.owner_token,
                           "--label",
                           "org.graphx.graph=" + state.graph_id,
                           "--network",
                           "none",
                           "--user",
                           "65532:65532",
                           "--cap-drop",
                           "ALL",
                           "--security-opt",
                           "no-new-privileges",
                           "--read-only",
                           "--log-driver",
                           "none",
                           "--pids-limit",
                           "64",
                           "--memory",
                           "256m",
                           "--mount",
                           "type=volume,src=" + volume.name + ",dst=/var/lib/graphx",
                           "--entrypoint",
                           "node",
                           image,
                           "--input-type=module",
                           "-e",
                           script,
                           "/app/credentials.mjs",
                           "-",
                           "/var/lib/graphx/credentials",
                           action->at("credential").text(),
                           action->at("next").text(),
                           std::to_string(action->at("grace_seconds").integer())});
      while (!created.empty() && (created.back() == '\n' || created.back() == '\r'))
        created.pop_back();
      state.processes.back().stable_id = created;
      save();
      // The compilation stays private; pass only its bounded, non-secret
      // credential manifest to the unprivileged helper through stdin.
      call({"docker", "start", "--attach", "--interactive", created},
           config_value_json(read(manifest)));
      const auto observed =
          parse_document(call({"docker", "container", "inspect", created})).array().front();
      require(observed.at("State").at("ExitCode") == Value(0),
              "credential tooling failed; recovery state retained");
      call({"docker", "rm", created});
      state.processes.pop_back();
    }
    record->identity = sha256(config_value_json(*action));
  }
  checkpoint();
  record->status = "complete";
  save();
  output << "scenario action complete: " << opts.scenario_action << '\n';
  return 0;
}
}  // namespace graphx::infra::detail
