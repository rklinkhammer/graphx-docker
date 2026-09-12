#include "graphx/ownership.hpp"

#include "infra/capture_resources.hpp"
#include "infra/command_runner.hpp"
#include "infra/endpoint_resources.hpp"
#include "infra/fault_resources.hpp"
#include "infra/lifecycle_coordinator.hpp"
#include "infra/namespace_resources.hpp"
#include "infra/ovs_resources.hpp"
#include "infra/ownership_lock.hpp"
#include "infra/ownership_state.hpp"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <signal.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace graphx {
using namespace infra::detail;
namespace {

using infra::detail::bridge_identity;
using infra::detail::configuration_hash;
using infra::detail::ensure_state_root;
using infra::detail::ExpectedCapture;
using infra::detail::ExpectedEndpoint;
using infra::detail::ExpectedFault;
using infra::detail::inspect_existing_state_root;
using infra::detail::load_state;
using infra::detail::OwnedCapture;
using infra::detail::OwnedFault;
using infra::detail::OwnershipLock;
using infra::detail::OwnershipLockMode;
using infra::detail::OwnershipState;
using infra::detail::path_entry_exists;
using infra::detail::random_token;
using infra::detail::save_state;
using infra::detail::stable_identity_matches;

class FileDescriptor {
 public:
  explicit FileDescriptor(int value = -1) : value_(value) {}
  ~FileDescriptor() {
    if (value_ >= 0) ::close(value_);
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  [[nodiscard]] int get() const noexcept { return value_; }

 private:
  int value_;
};

int run(const std::vector<std::string>& arguments, std::string* captured = nullptr) {
  infra::detail::CommandOptions options;
  options.arguments = arguments;
  options.capture_output = captured != nullptr;
  auto result = infra::detail::run_command(options);
  if (captured) *captured = std::move(result.output);
  if (result.output_truncated) throw std::runtime_error("command output exceeded safety limit");
  return result.status;
}

void inject_test_interruption(std::size_t mutation) {
  const auto matches = [mutation](const char* name) {
    const auto* value = std::getenv(name);
    return value != nullptr && std::to_string(mutation) == value;
  };
  if (matches("GRAPHX_TEST_CRASH_AFTER_MUTATION"))
    ::_exit(99);  // Deliberate test-only crash point; the ledger enables recovery.
  if (matches("GRAPHX_TEST_FAIL_AFTER_MUTATION"))
    throw std::runtime_error("injected interruption after mutation " + std::to_string(mutation));
}

}  // namespace

std::filesystem::path infra::detail::default_ownership_state_root_impl() {
  if (const auto* configured = std::getenv("GRAPHX_STATE_DIR")) return configured;
  return "/var/lib/graphx/runs";
}

int infra::detail::execute_ovs_lifecycle_impl(const GraphConfig& config,
                                              const std::filesystem::path& config_path,
                                              OvsLifecycleAction action, bool dry_run,
                                              const std::filesystem::path& state_root,
                                              std::ostream& output, std::ostream& errors) {
  if (config.version != 2) throw std::invalid_argument("OVS lifecycle requires version 2");
  const auto hash = configuration_hash(config_path);
  const auto state_path = state_root / (config.id + ".yaml");
  if (dry_run) {
    if (action == OvsLifecycleAction::create) {
      output << "# ownership-state " << state_path << "\n";
      for (const auto& item : config.network_infrastructure.switches)
        output << planned_create(item, config.id, "<generated-owner-token>", hash) << '\n';
      for (const auto& router : config.network_infrastructure.routers)
        if (router.kind == RouterKind::linux_namespace)
          output << "ip netns add " << router.namespace_name << " # owner=" << router.id << '\n';
      for (const auto& item : config.network_infrastructure.attachments) {
        if (item.kind == AttachmentKind::container_veth) {
          output << "docker[project=" << config.deployment.project << ",service=" << item.owner
                 << "] -> ip link add " << item.peer << " type veth peer name "
                 << item.interface << " -> ovs-vsctl add-port " << item.network_switch << ' '
                 << item.peer << " -> netns[verified-container] address=" << item.address
                 << " mtu=" << item.mtu << '\n';
        } else if (item.kind == AttachmentKind::namespace_veth) {
          output << "netns[" << item.owner << "] -> ip link add " << item.peer
                 << " type veth peer name " << item.interface << " -> ovs-vsctl add-port "
                 << item.network_switch << ' ' << item.peer << " address=" << item.address << '\n';
        } else if (item.kind == AttachmentKind::qemu_tap) {
          output << "ip tuntap add dev " << item.interface << " mode tap user " << item.tap_uid
                 << " group " << item.tap_gid << " -> ovs-vsctl add-port " << item.network_switch
                 << ' ' << item.interface << " mtu=" << item.mtu << '\n';
        } else if (item.kind == AttachmentKind::mirror) {
          output << "mirror " << item.id << " -> ovs-vsctl add-port " << item.network_switch << ' '
                 << item.interface << " -> OVS SPAN\n";
        } else if (item.kind == AttachmentKind::external) {
          output << "external-boundary " << item.id << " owner=" << item.owner << " unchanged\n";
        }
        for (const auto& route : item.routes) {
          output << "netns[" << item.owner << "] ip route replace " << route.destination;
          if (!route.via.empty()) output << " via " << route.via;
          output << " dev " << item.interface << '\n';
        }
      }
      for (const auto& capture : config.network_infrastructure.captures)
        output << "dumpcap attachment=" << capture.attachment << " format=ethernet-pcapng"
               << " directory=" << capture.directory << " snaplen=" << capture.snaplen
               << " max-file-bytes=" << capture.max_file_bytes << " max-files=" << capture.max_files
               << " rotate-seconds=" << capture.rotation_seconds
               << " retention-seconds=" << capture.retention_seconds << '\n';
      for (const auto& fault : config.network_infrastructure.faults)
        output << "tc qdisc replace attachment=" << fault.attachment << " root netem"
               << " delay-ms=" << fault.delay_ms << " jitter-ms=" << fault.jitter_ms
               << " loss-percent=" << fault.loss_percent << " rate-kbit=" << fault.rate_kbit
               << " duration-seconds=" << fault.duration_seconds
               << " auto-clear=identity-checked\n";
      for (const auto& network : config.network_infrastructure.networks) {
        if (!network.profile || (*network.profile != NetworkProfile::ipvlan_l2 &&
                                 *network.profile != NetworkProfile::ipvlan_l3 &&
                                 *network.profile != NetworkProfile::ipvlan_l3s))
          continue;
        std::unordered_set<std::string> switches;
        for (const auto& item : config.network_infrastructure.attachments) {
          if (item.network != network.id || item.address.empty() ||
              (item.kind != AttachmentKind::container_veth &&
               item.kind != AttachmentKind::namespace_veth))
            continue;
          switches.insert(item.network_switch);
          output << "ovs-ofctl add-flow " << item.network_switch
                 << " profile=" << to_string(*network.profile)
                 << " ip-dst=" << address_host(item.address)
                 << " arp-target=" << address_host(item.address)
                 << " output=<resolved-ofport:" << item.peer << ">\n";
        }
        for (const auto& network_switch : switches)
          output << "ovs-ofctl add-flow " << network_switch
                 << " ipvlan-broadcast-multicast=drop-except-targeted-arp\n";
      }
      for (const auto& router : config.network_infrastructure.routers) {
        if (router.kind != RouterKind::linux_namespace) continue;
        if (router.forwarding)
          output << "ip netns exec " << router.namespace_name
                 << " sysctl -q -w net.ipv4.ip_forward=1\n";
        for (const auto& route : router.routes) {
          output << "router-route " << router.id << ' ' << route.destination;
          if (!route.via.empty()) output << " via " << route.via;
          if (!route.device.empty()) output << " dev " << route.device;
          output << " install=" << (route.install_on_create ? "create" : "manual") << '\n';
        }
        if (!router.policies.empty())
          output << "nft add table/chain netns=" << router.namespace_name
                 << " family=inet table=graphx chain=forward policy=drop\n"
                 << "nft add rule netns=" << router.namespace_name
                 << " policy=established-return ct-state=established,related action=accept\n";
        for (const auto& policy : router.policies)
          output << "nft add rule netns=" << router.namespace_name << " policy=" << policy.id
                 << " source=" << policy.source << " destination=" << policy.destination
                 << " action=" << policy.action << '\n';
      }
    } else {
      output << "# " << (action == OvsLifecycleAction::status ? "inspect" : "identity-check-delete")
             << " ownership-state " << state_path << '\n';
    }
    return 0;
  }

  const auto lock_path = state_root / (config.id + ".lock");
  if (action == OvsLifecycleAction::status) {
    if (!inspect_existing_state_root(state_root) || !path_entry_exists(state_path)) {
      output << "No OVS ownership state for graph " << config.id << '\n';
      return 2;
    }
    auto lock = OwnershipLock::open_existing(lock_path, OwnershipLockMode::shared);
    const auto state = load_state(state_path);
    if (state.graph_id != config.id) throw std::runtime_error("ownership state graph mismatch");
    const bool config_matches = state.config_hash == hash;
    if (!config_matches)
      errors << "graphx: current configuration differs from the ownership ledger; resource "
                "identity still controls cleanup\n";
    bool healthy = state.status == "ready" && config_matches;
    output << "graph=" << state.graph_id << " status=" << state.status
           << " config-sha256=" << state.config_hash
           << " current-config=" << (config_matches ? "matched" : "drifted") << '\n';
    for (const auto& item : state.bridges) {
      const bool owned = bridge_owned(item, state);
      output << "ovs_bridge " << item.name << " uuid=" << item.stable_id
             << " state=" << (owned ? "owned" : "missing-or-replaced") << '\n';
      healthy = healthy && owned;
    }
    for (const auto& item : state.endpoints) {
      const auto& expected = expected_endpoint(state, item.attachment_id);
      bool owned{};
      try {
        if (expected.kind == AttachmentKind::container_veth)
          owned = container_endpoint_healthy(expected, item, state, config);
        else if (expected.kind == AttachmentKind::namespace_veth)
          owned = namespace_endpoint_healthy(expected, item, state);
        else if (expected.kind == AttachmentKind::qemu_tap)
          owned = tap_endpoint_healthy(expected, item, state);
        else
          owned = mirror_endpoint_healthy(expected, item, state);
      } catch (const std::exception& error) {
        errors << "graphx: endpoint identity check failed for " << item.attachment_id << ": "
               << error.what() << '\n';
      }
      output << to_string(expected.kind) << ' ' << item.attachment_id << " host=" << item.name;
      if (!item.container_id.empty()) output << " container=" << item.container_id;
      if (!item.tap_owner.empty()) output << " tap-owner=" << item.tap_owner;
      output << " netns-inode=" << *item.namespace_inode
             << " state=" << (owned ? "owned" : "missing-replaced-or-restarted") << '\n';
      healthy = healthy && owned;
    }
    for (const auto& item : state.namespaces) {
      const bool owned = namespace_owned(item, state);
      output << "linux_namespace " << item.name << " netns-inode=" << *item.namespace_inode
             << " state=" << (owned ? "owned" : "missing-or-replaced") << '\n';
      healthy = healthy && owned;
    }
    for (const auto& item : state.captures) {
      const auto expected =
          std::find_if(state.expected_captures.begin(), state.expected_captures.end(),
                       [&](const auto& candidate) { return candidate.definition.id == item.id; });
      const bool owned =
          expected != state.expected_captures.end() && capture_healthy(*expected, item);
      output << "network_capture " << item.id << " attachment=" << item.attachment_id
             << " interface=" << item.interface << " pid=" << item.pid
             << " directory=" << item.session_directory
             << " state=" << (owned ? "capturing" : "missing-replaced-or-completed") << '\n';
      healthy = healthy && owned;
    }
    for (const auto& item : state.faults) {
      const bool owned = fault_healthy(item);
      const bool active = timer_owned(item.timer_pid, item.timer_start_time);
      output << "netem_fault " << item.id << " attachment=" << item.attachment_id
             << " interface=" << item.interface << " state="
             << (owned ? (active ? "active" : "expired") : "missing-or-replaced") << '\n';
      healthy = healthy && owned;
    }
    return healthy ? 0 : 2;
  }

  ensure_state_root(state_root);
  // Refuse even a dangling symlink before create mutates the per-graph state
  // directory by opening or creating its lock.
  if (action == OvsLifecycleAction::create && path_entry_exists(state_path))
    throw std::runtime_error("ownership state already exists; inspect, destroy, or recover it");
  auto lock = OwnershipLock::open_or_create(lock_path, OwnershipLockMode::exclusive);

  if (action == OvsLifecycleAction::create) {
    if (path_entry_exists(state_path))
      throw std::runtime_error("ownership state already exists; inspect, destroy, or recover it");
    for (const auto& item : config.network_infrastructure.switches)
      if (bridge_exists(item.id))
        throw std::runtime_error("refusing unowned OVS bridge collision: " + item.id);
    OwnershipState state;
    state.graph_id = config.id;
    state.config_hash = hash;
    state.owner_token = random_token();
    state.status = "creating";
    for (const auto& item : config.network_infrastructure.switches)
      state.expected_bridges.push_back(item.id);
    std::unordered_map<std::string, ResolvedContainer> containers;
    std::unordered_map<std::string, const RouterDefinition*> routers;
    for (const auto& router : config.network_infrastructure.routers) {
      if (router.kind != RouterKind::linux_namespace) continue;
      if (namespace_inode(router.namespace_name))
        throw std::runtime_error("refusing unowned Linux namespace collision: " +
                                 router.namespace_name);
      state.expected_namespaces.push_back(router.namespace_name);
      routers.emplace(router.id, &router);
    }
    struct stat host_namespace{};
    if (::stat("/proc/self/ns/net", &host_namespace) != 0)
      throw std::runtime_error("cannot inspect host network namespace");
    for (const auto& item : config.network_infrastructure.attachments) {
      if (item.kind != AttachmentKind::container_veth &&
          item.kind != AttachmentKind::namespace_veth && item.kind != AttachmentKind::qemu_tap &&
          item.kind != AttachmentKind::mirror)
        continue;
      if (item.kind != AttachmentKind::mirror && item.address.empty())
        throw std::runtime_error("network realization requires a declared address for attachment " +
                                 item.id);
      ExpectedEndpoint endpoint;
      endpoint.kind = item.kind;
      endpoint.id = item.id;
      endpoint.owner = item.owner;
      endpoint.host_interface = item.peer;
      endpoint.target_interface = item.interface;
      endpoint.network_switch = item.network_switch;
      endpoint.address = item.address;
      endpoint.mac = item.mac;
      endpoint.mtu = item.mtu;
      endpoint.routes = item.routes;
      endpoint.tap_uid = item.tap_uid;
      endpoint.tap_gid = item.tap_gid;
      if (item.kind == AttachmentKind::container_veth) {
        auto [container_entry, inserted] = containers.try_emplace(item.owner);
        if (inserted) container_entry->second = resolve_container(config, item.owner);
        const auto& container = container_entry->second;
        endpoint.container_id = container.id;
        endpoint.namespace_inode = container.namespace_inode;
        check_endpoint_collision(endpoint, container.pid);
      } else if (item.kind == AttachmentKind::namespace_veth) {
        const auto router = routers.find(item.owner);
        if (router == routers.end())
          throw std::runtime_error("missing Linux namespace router for " + item.id);
        endpoint.namespace_name = router->second->namespace_name;
        endpoint.namespace_inode = 1;  // Replaced with the owned inode after namespace creation.
        if (link_ifindex(endpoint.host_interface) ||
            !ovs_get("Port", endpoint.host_interface, "_uuid").empty())
          throw std::runtime_error("refusing namespace endpoint collision: " + item.id);
      } else if (item.kind == AttachmentKind::qemu_tap) {
        endpoint.host_interface = item.interface;
        endpoint.target_interface = item.interface;
        endpoint.namespace_inode = static_cast<std::uint64_t>(host_namespace.st_ino);
        if (link_ifindex(endpoint.host_interface) ||
            !ovs_get("Port", endpoint.host_interface, "_uuid").empty() ||
            !ovs_get("Interface", endpoint.host_interface, "_uuid").empty())
          throw std::runtime_error("refusing QEMU TAP collision: " + item.id);
      } else {
        const auto& network_switch =
            config.network_infrastructure.network_switch(item.network_switch);
        const auto port =
            std::find_if(network_switch.ports.begin(), network_switch.ports.end(),
                         [&](const auto& value) { return value.interface == item.interface; });
        if (port == network_switch.ports.end() || port->peer.empty())
          throw std::runtime_error("mirror attachment requires a switch port peer: " + item.id);
        endpoint.host_interface = item.interface;
        endpoint.target_interface = port->peer;
        endpoint.namespace_inode = static_cast<std::uint64_t>(host_namespace.st_ino);
        if (link_ifindex(endpoint.host_interface) || link_ifindex(endpoint.target_interface) ||
            !ovs_get("Port", endpoint.host_interface, "_uuid").empty())
          throw std::runtime_error("refusing mirror endpoint collision: " + item.id);
      }
      const auto& endpoint_switch =
          config.network_infrastructure.network_switch(endpoint.network_switch);
      const auto endpoint_port = std::find_if(endpoint_switch.ports.begin(),
                                              endpoint_switch.ports.end(), [&](const auto& value) {
                                                return value.interface == endpoint.host_interface ||
                                                       value.peer == endpoint.host_interface;
                                              });
      if (endpoint_port != endpoint_switch.ports.end()) endpoint.vlan = endpoint_port->vlan;
      state.expected_endpoints.push_back(std::move(endpoint));
    }
    for (const auto& capture : config.network_infrastructure.captures) {
      const auto endpoint =
          std::find_if(state.expected_endpoints.begin(), state.expected_endpoints.end(),
                       [&](const auto& candidate) { return candidate.id == capture.attachment; });
      if (endpoint == state.expected_endpoints.end() || endpoint->kind != AttachmentKind::mirror)
        throw std::runtime_error("network capture references unrealized mirror " +
                                 capture.attachment);
      state.expected_captures.push_back({capture, endpoint->target_interface});
    }
    for (const auto& fault : config.network_infrastructure.faults) {
      const auto endpoint =
          std::find_if(state.expected_endpoints.begin(), state.expected_endpoints.end(),
                       [&](const auto& candidate) { return candidate.id == fault.attachment; });
      if (endpoint == state.expected_endpoints.end() || endpoint->kind == AttachmentKind::mirror)
        throw std::runtime_error("network fault references unrealized data attachment " +
                                 fault.attachment);
      state.expected_faults.push_back({fault, endpoint->host_interface});
    }
    save_state(state_path, state, false);
    try {
      std::size_t mutation{};
      for (const auto& item : config.network_infrastructure.switches) {
        std::string uuid;
        const std::vector<std::string> command = {
            "ovs-vsctl",
            "--",
            "add-br",
            item.id,
            "--",
            "set",
            "Bridge",
            item.id,
            "datapath_type=system",
            "external_ids:graphx_owner=" + state.owner_token,
            "external_ids:graphx_config_hash=" + state.config_hash,
            "external_ids:graphx_graph=" + state.graph_id};
        output << "+ " << planned_create(item, state.graph_id, state.owner_token, state.config_hash)
               << '\n';
        if (run(command) != 0)
          throw std::runtime_error("cannot atomically create owned OVS bridge " + item.id);
        uuid = ovs_get(item.id, "_uuid");
        if (uuid.empty()) throw std::runtime_error("cannot capture owned OVS bridge identity");
        ++mutation;
        inject_test_interruption(mutation);
        const auto internal_port_uuid = ovs_get("Port", item.id, "_uuid");
        if (internal_port_uuid.empty())
          throw std::runtime_error("cannot capture owned OVS internal port identity");
        state.bridges.push_back(bridge_identity(item.id, uuid, internal_port_uuid));
        save_state(state_path, state);
      }
      for (const auto& [id, router] : routers) {
        output << "+ linux_namespace " << router->namespace_name << '\n';
        state.namespaces.push_back(create_namespace(*router, state));
        for (auto& endpoint : state.expected_endpoints)
          if (endpoint.owner == id && endpoint.kind == AttachmentKind::namespace_veth)
            endpoint.namespace_inode = *state.namespaces.back().namespace_inode;
        save_state(state_path, state);
        ++mutation;
        inject_test_interruption(mutation);
      }
      for (const auto& endpoint : state.expected_endpoints) {
        output << "+ " << to_string(endpoint.kind) << ' ' << endpoint.id
               << " host=" << endpoint.host_interface << " switch=" << endpoint.network_switch
               << '\n';
        if (endpoint.kind == AttachmentKind::container_veth)
          state.endpoints.push_back(
              create_endpoint(endpoint, containers.at(endpoint.owner).pid, state));
        else if (endpoint.kind == AttachmentKind::namespace_veth)
          state.endpoints.push_back(create_namespace_endpoint(endpoint, state));
        else if (endpoint.kind == AttachmentKind::qemu_tap)
          state.endpoints.push_back(create_tap_endpoint(endpoint, state));
        else
          state.endpoints.push_back(create_mirror_endpoint(endpoint, state));
        ++mutation;
        inject_test_interruption(mutation);
        save_state(state_path, state);
      }
      install_profile_flows(config, state);
      for (const auto& [id, router] : routers) {
        if (router->forwarding && run({"ip", "netns", "exec", router->namespace_name, "sysctl",
                                       "-q", "-w", "net.ipv4.ip_forward=1"}) != 0)
          throw std::runtime_error("cannot enable forwarding for router " + id);
        for (const auto& route : router->routes) {
          if (!route.install_on_create) continue;
          std::vector<std::string> command = {"ip", "netns", "exec",    router->namespace_name,
                                              "ip", "route", "replace", route.destination};
          if (!route.via.empty()) command.insert(command.end(), {"via", route.via});
          if (!route.device.empty()) command.insert(command.end(), {"dev", route.device});
          if (run(command) != 0) throw std::runtime_error("cannot install router route");
        }
        if (!router->policies.empty()) {
          if (run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "table", "inet",
                   "graphx"}) != 0 ||
              run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "chain", "inet",
                   "graphx", "forward", "{ type filter hook forward priority 0; policy drop; }"}) !=
                  0 ||
              run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "rule", "inet",
                   "graphx", "forward", "ct", "state", "established,related", "counter",
                   "accept"}) != 0)
            throw std::runtime_error("cannot create router policy table");
          for (const auto& policy : router->policies)
            if (run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "rule", "inet",
                     "graphx", "forward", "ip", "saddr", policy.source, "ip", "daddr",
                     policy.destination, "counter", policy.action}) != 0)
              throw std::runtime_error("cannot install router policy " + policy.id);
        }
      }
      for (const auto& capture : state.expected_captures) {
        output << "+ network_capture " << capture.definition.id
               << " interface=" << capture.interface << '\n';
        state.captures.push_back(create_capture(capture, state));
        save_state(state_path, state);
        ++mutation;
        inject_test_interruption(mutation);
      }
      for (const auto& fault : state.expected_faults) {
        output << "+ netem_fault " << fault.definition.id << " interface="
               << fault.interface << " duration=" << fault.definition.duration_seconds << "s\n";
        state.faults.push_back(create_fault(fault));
        save_state(state_path, state);
        ++mutation;
        inject_test_interruption(mutation);
      }
      state.status = "ready";
      save_state(state_path, state);
      output << "GraphX OVS laboratory state ready: " << state_path << '\n';
      return 0;
    } catch (...) {
      errors << "graphx: create interrupted; recovering owned OVS/container mutations\n";
      for (auto iterator = state.faults.rbegin(); iterator != state.faults.rend(); ++iterator)
        if (!clear_fault(*iterator))
          errors << "graphx: retained ownership state after rollback could not safely clear fault "
                 << iterator->id << '\n';
      for (auto iterator = state.captures.rbegin(); iterator != state.captures.rend(); ++iterator)
        if (!stop_capture(*iterator))
          errors << "graphx: retained ownership state after rollback could not safely stop capture "
                 << iterator->id << '\n';
      for (const auto& expected : state.expected_endpoints) {
        const bool recorded =
            std::any_of(state.endpoints.begin(), state.endpoints.end(),
                        [&](const auto& item) { return item.attachment_id == expected.id; });
        if (recorded) continue;
        const auto ifindex = link_ifindex(expected.host_interface);
        const auto port_uuid = ovs_get("Port", expected.host_interface, "_uuid");
        if (!ifindex && port_uuid.empty()) continue;
        OwnedResourceIdentity discovered;
        discovered.kind =
            expected.kind == AttachmentKind::container_veth
                ? "container_veth"
                : (expected.kind == AttachmentKind::namespace_veth
                       ? "namespace_veth"
                       : (expected.kind == AttachmentKind::qemu_tap ? "qemu_tap" : "mirror_veth"));
        discovered.attachment_id = expected.id;
        discovered.name = expected.host_interface;
        discovered.target_interface = expected.target_interface;
        discovered.ifindex = ifindex.value_or(0);
        discovered.peer_ifindex = expected.kind == AttachmentKind::mirror
                                      ? link_ifindex(expected.target_interface).value_or(0)
                                      : ifindex.value_or(0);
        discovered.namespace_inode = expected.namespace_inode;
        discovered.container_id = expected.container_id;
        if (expected.kind == AttachmentKind::qemu_tap)
          discovered.tap_owner = tap_owner_identity(expected.tap_uid, expected.tap_gid);
        discovered.stable_id = port_uuid;
        discovered.secondary_id = ovs_get("Interface", expected.host_interface, "_uuid");
        if (expected.kind == AttachmentKind::mirror)
          discovered.route_identity =
              ovs_find_uuid("Mirror", "external_ids:graphx_attachment", expected.id);
        state.endpoints.push_back(std::move(discovered));
      }
      // Include atomic mutations that happened before their ledger update.
      for (const auto& name : state.expected_bridges) {
        bool recorded{};
        for (const auto& item : state.bridges) recorded = recorded || item.name == name;
        if (recorded) continue;
        if (!bridge_exists(name)) continue;
        const auto discovered =
            bridge_identity(name, ovs_get(name, "_uuid"), ovs_get("Port", name, "_uuid"));
        if (ovs_get(name, "external_ids:graphx_owner") == state.owner_token &&
            ovs_get(name, "external_ids:graphx_config_hash") == state.config_hash &&
            ovs_get(name, "external_ids:graphx_graph") == state.graph_id)
          state.bridges.push_back(discovered);
      }
      bool cleanup_complete =
          std::ranges::all_of(state.faults,
                              [&](const auto& item) {
                                return fault_healthy(item) &&
                                       qdisc_state(item.interface).find("netem") ==
                                           std::string::npos;
                              }) &&
          std::ranges::all_of(state.captures, [&](const auto& item) {
            return !process_owned(item.pid, item.process_start_time,
                                  item.session_directory.string());
          });
      for (auto iterator = state.endpoints.rbegin(); iterator != state.endpoints.rend(); ++iterator)
        if (!delete_owned_endpoint(*iterator, state)) {
          cleanup_complete = false;
          errors << "graphx: retained ownership state after rollback could not safely remove "
                 << iterator->attachment_id << '\n';
        }
      for (auto iterator = state.namespaces.rbegin(); iterator != state.namespaces.rend();
           ++iterator)
        if (!delete_owned_namespace(*iterator, state)) {
          cleanup_complete = false;
          errors << "graphx: retained ownership state after rollback could not safely remove "
                 << iterator->name << '\n';
        }
      for (auto iterator = state.bridges.rbegin(); iterator != state.bridges.rend(); ++iterator)
        if (bridge_exists(iterator->name)) {
          if (!bridge_owned(*iterator, state) || !delete_owned_bridge(*iterator, state)) {
            cleanup_complete = false;
            errors << "graphx: retained ownership state after rollback could not safely remove "
                   << iterator->name << '\n';
          }
        }
      if (cleanup_complete)
        std::filesystem::remove(state_path);
      else
        save_state(state_path, state);
      throw;
    }
  }

  if (!path_entry_exists(state_path)) {
    throw std::runtime_error("no ownership state for graph " + config.id);
  }
  auto state = load_state(state_path);
  if (state.graph_id != config.id) throw std::runtime_error("ownership state graph mismatch");
  const bool config_matches = state.config_hash == hash;
  if (!config_matches)
    errors << "graphx: current configuration differs from the ownership ledger; resource identity "
              "still controls cleanup\n";

  if (action == OvsLifecycleAction::recover && state.status == "ready")
    throw std::runtime_error("ownership state is ready; use infra destroy, not recover");

  // Recover an atomic bridge mutation that completed before its state update.
  for (const auto& name : state.expected_bridges) {
    bool recorded{};
    for (const auto& item : state.bridges) recorded = recorded || item.name == name;
    if (recorded || !bridge_exists(name)) continue;
    if (ovs_get(name, "external_ids:graphx_owner") != state.owner_token ||
        ovs_get(name, "external_ids:graphx_config_hash") != state.config_hash ||
        ovs_get(name, "external_ids:graphx_graph") != state.graph_id)
      throw std::runtime_error("refusing unowned or replaced OVS bridge: " + name);
    state.bridges.push_back(
        bridge_identity(name, ovs_get(name, "_uuid"), ovs_get("Port", name, "_uuid")));
  }
  // Recover an endpoint mutation completed before its ledger update by requiring
  // both the intrinsic host alias and OVS ownership markers.
  for (const auto& expected : state.expected_endpoints) {
    const bool recorded =
        std::any_of(state.endpoints.begin(), state.endpoints.end(),
                    [&](const auto& item) { return item.attachment_id == expected.id; });
    if (recorded) continue;
    const auto ifindex = link_ifindex(expected.host_interface);
    const auto port_uuid = ovs_get("Port", expected.host_interface, "_uuid");
    if (!ifindex && port_uuid.empty()) continue;
    if (!ifindex ||
        link_alias(expected.host_interface) != endpoint_alias(state, expected.id, "host") ||
        port_uuid.empty() ||
        ovs_get("Port", port_uuid, "external_ids:graphx_owner") != state.owner_token ||
        ovs_get("Port", port_uuid, "external_ids:graphx_attachment") != expected.id)
      throw std::runtime_error("refusing unowned or replaced container endpoint: " + expected.id);
    OwnedResourceIdentity discovered;
    discovered.kind =
        expected.kind == AttachmentKind::container_veth
            ? "container_veth"
            : (expected.kind == AttachmentKind::namespace_veth
                   ? "namespace_veth"
                   : (expected.kind == AttachmentKind::qemu_tap ? "qemu_tap" : "mirror_veth"));
    discovered.attachment_id = expected.id;
    discovered.name = expected.host_interface;
    discovered.target_interface = expected.target_interface;
    discovered.ifindex = *ifindex;
    // Container and namespace cleanup only needs the host identity. Mirrors
    // retain both host-side links, so recover their actual peer identity for
    // the complete-set replacement check.
    if (expected.kind == AttachmentKind::mirror) {
      const auto peer_ifindex = link_ifindex(expected.target_interface);
      if (!peer_ifindex ||
          link_alias(expected.target_interface) != endpoint_alias(state, expected.id, "peer"))
        throw std::runtime_error("refusing unowned or replaced mirror peer: " + expected.id);
      discovered.peer_ifindex = *peer_ifindex;
    } else {
      discovered.peer_ifindex = *ifindex;
    }
    discovered.namespace_inode = expected.namespace_inode;
    discovered.container_id = expected.container_id;
    if (expected.kind == AttachmentKind::qemu_tap) {
      if (!tap_owner_matches(expected))
        throw std::runtime_error("refusing unowned or replaced QEMU TAP: " + expected.id);
      discovered.tap_owner = tap_owner_identity(expected.tap_uid, expected.tap_gid);
    }
    discovered.stable_id = port_uuid;
    discovered.secondary_id = ovs_get("Interface", expected.host_interface, "_uuid");
    if (expected.kind == AttachmentKind::mirror)
      discovered.route_identity =
          ovs_find_uuid("Mirror", "external_ids:graphx_attachment", expected.id);
    state.endpoints.push_back(std::move(discovered));
  }
  // Refuse the entire cleanup before the first mutation when any recorded
  // resource has been replaced. Each item is checked again immediately before
  // its own deletion to close the remaining race window.
  for (const auto& item : state.bridges)
    if (bridge_exists(item.name) && !bridge_complete_set_owned(item, state))
      throw std::runtime_error("refusing OVS bridge with replaced identity or unexpected ports: " +
                               item.name);
  for (const auto& item : state.endpoints) {
    if (link_ifindex(item.name) && !host_endpoint_owned(item, state))
      throw std::runtime_error("refusing to delete replaced host endpoint: " + item.attachment_id);
    if (!ovs_get("Port", item.stable_id, "_uuid").empty() && !ovs_endpoint_owned(item, state))
      throw std::runtime_error("refusing to delete replaced OVS endpoint: " + item.attachment_id);
    if (!endpoint_names_absent_or_recorded(item))
      throw std::runtime_error("refusing same-name OVS endpoint replacement: " +
                               item.attachment_id);
    if (!item.route_identity.empty() &&
        (ovs_get("Mirror", item.route_identity, "external_ids:graphx_owner") != state.owner_token ||
         ovs_get("Mirror", item.route_identity, "external_ids:graphx_attachment") !=
             item.attachment_id))
      throw std::runtime_error("refusing replaced OVS mirror: " + item.attachment_id);
    const auto& expected = expected_endpoint(state, item.attachment_id);
    if (expected.kind == AttachmentKind::qemu_tap &&
        (!tap_owner_matches(expected) ||
         item.tap_owner != tap_owner_identity(expected.tap_uid, expected.tap_gid) ||
         !endpoint_vlan_matches(expected, item)))
      throw std::runtime_error("refusing replaced QEMU TAP ownership or VLAN: " +
                               item.attachment_id);
    if (expected.kind == AttachmentKind::mirror &&
        (link_ifindex(expected.target_interface) != item.peer_ifindex ||
         link_alias(expected.target_interface) !=
             endpoint_alias(state, item.attachment_id, "peer")))
      throw std::runtime_error("refusing replaced mirror peer: " + item.attachment_id);
  }
  for (const auto& item : state.namespaces)
    if (namespace_inode(item.name) && !namespace_owned(item, state))
      throw std::runtime_error("refusing to delete replaced Linux namespace: " + item.name);
  for (const auto& item : state.captures) {
    if (!directory_identity_matches(item))
      throw std::runtime_error("refusing replaced capture directory: " + item.id);
    const auto start = process_start_time(item.pid);
    if (!start.empty() &&
        !process_owned(item.pid, item.process_start_time, item.session_directory.string()))
      throw std::runtime_error("refusing replaced capture process: " + item.id);
  }
  for (const auto& item : state.faults)
    if (!fault_healthy(item))
      throw std::runtime_error("refusing replaced fault target, timer, or qdisc: " + item.id);
  state.status = "destroying";
  save_state(state_path, state);
  for (auto iterator = state.faults.rbegin(); iterator != state.faults.rend(); ++iterator) {
    output << "- netem_fault " << iterator->id << '\n';
    if (!clear_fault(*iterator))
      throw std::runtime_error("identity changed while clearing network fault " + iterator->id);
  }
  for (auto iterator = state.captures.rbegin(); iterator != state.captures.rend(); ++iterator) {
    output << "- network_capture " << iterator->id << " retained=" << iterator->session_directory
           << '\n';
    if (!stop_capture(*iterator))
      throw std::runtime_error("identity changed while stopping network capture " + iterator->id);
  }
  for (auto iterator = state.endpoints.rbegin(); iterator != state.endpoints.rend(); ++iterator) {
    output << "- " << iterator->kind << ' ' << iterator->attachment_id << '\n';
    if (!delete_owned_endpoint(*iterator, state))
      throw std::runtime_error("identity changed while deleting owned container endpoint " +
                               iterator->attachment_id);
  }
  for (auto iterator = state.namespaces.rbegin(); iterator != state.namespaces.rend(); ++iterator) {
    output << "- linux_namespace " << iterator->name << '\n';
    if (!delete_owned_namespace(*iterator, state))
      throw std::runtime_error("identity changed while deleting Linux namespace " + iterator->name);
  }
  for (auto iterator = state.bridges.rbegin(); iterator != state.bridges.rend(); ++iterator) {
    if (!bridge_exists(iterator->name)) continue;
    if (!bridge_owned(*iterator, state))
      throw std::runtime_error("refusing to delete replaced OVS bridge: " + iterator->name);
    output << "- ovs-vsctl del-br " << iterator->name << '\n';
    if (!delete_owned_bridge(*iterator, state))
      throw std::runtime_error("identity changed while deleting owned OVS bridge " +
                               iterator->name);
  }
  std::filesystem::remove(state_path);
  output << "GraphX OVS laboratory state removed\n";
  return 0;
}

int infra::detail::export_owned_network_capture_impl(const GraphConfig& config,
                                                     const std::filesystem::path& config_path,
                                                     std::string_view capture_id,
                                                     const std::filesystem::path& state_root,
                                                     const std::filesystem::path& destination,
                                                     std::ostream& output) {
  if (config.version != 2) throw std::invalid_argument("network capture export requires version 2");
  if (!inspect_existing_state_root(state_root))
    throw std::runtime_error("network capture ownership state is unavailable");
  const auto state_path = state_root / (config.id + ".yaml");
  const auto lock_path = state_root / (config.id + ".lock");
  auto lock = OwnershipLock::open_existing(lock_path, OwnershipLockMode::shared);
  const auto state = load_state(state_path);
  if (state.graph_id != config.id || state.config_hash != configuration_hash(config_path) ||
      state.status != "ready")
    throw std::runtime_error("network capture export requires matching ready ownership state");
  const auto capture = std::find_if(state.captures.begin(), state.captures.end(),
                                    [&](const auto& item) { return item.id == capture_id; });
  const auto expected =
      std::find_if(state.expected_captures.begin(), state.expected_captures.end(),
                   [&](const auto& item) { return item.definition.id == capture_id; });
  if (capture == state.captures.end() || expected == state.expected_captures.end() ||
      !capture_healthy(*expected, *capture))
    throw std::runtime_error("network capture is missing, replaced, or unhealthy");
  const FileDescriptor directory(
      ::open(capture->session_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  struct stat directory_metadata{};
  if (directory.get() < 0 || ::fstat(directory.get(), &directory_metadata) != 0 ||
      !capture_directory_metadata_matches(*capture, directory_metadata))
    throw std::runtime_error("network capture directory security identity changed");
  const auto duplicate = ::dup(directory.get());
  if (duplicate < 0) throw std::system_error(errno, std::generic_category());
  auto* entries = ::fdopendir(duplicate);
  if (!entries) {
    const auto failure = errno;
    ::close(duplicate);
    throw std::system_error(failure, std::generic_category(),
                            "cannot inspect network capture directory");
  }
  std::string selected;
  time_t newest{};
  bool enumeration_failed = false;
  for (;;) {
    errno = 0;
    const auto* entry = ::readdir(entries);
    if (!entry) {
      enumeration_failed = errno != 0;
      break;
    }
    const std::string name = entry->d_name;
    if (!name.ends_with(".pcapng")) continue;
    struct stat metadata{};
    if (::fstatat(directory.get(), name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
        !capture_file_metadata_is_safe(*capture, metadata))
      continue;
    if (selected.empty() || metadata.st_mtime >= newest) {
      selected = name;
      newest = metadata.st_mtime;
    }
  }
  ::closedir(entries);
  if (enumeration_failed) throw std::runtime_error("cannot enumerate network capture directory");
  if (selected.empty()) throw std::runtime_error("network capture has no exportable PCAPNG file");
  FileDescriptor source(::openat(directory.get(), selected.c_str(), O_RDONLY | O_NOFOLLOW));
  struct stat metadata{};
  if (source.get() < 0 || ::fstat(source.get(), &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      !capture_file_metadata_is_safe(*capture, metadata) || metadata.st_size < 0 ||
      static_cast<std::uint64_t>(metadata.st_size) >
          expected->definition.max_file_bytes + expected->definition.snaplen + 4096 ||
      !complete_pcapng_snapshot(source.get(), static_cast<std::uint64_t>(metadata.st_size)))
    throw std::runtime_error("network capture file is unsafe, oversized, or incomplete");
  FileDescriptor target(
      ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600));
  if (target.get() < 0)
    throw std::system_error(errno, std::generic_category(), "cannot create capture export");
  std::array<char, 64 * 1024> buffer{};
  std::uint64_t remaining = static_cast<std::uint64_t>(metadata.st_size);
  while (remaining > 0) {
    const auto wanted = std::min<std::uint64_t>(remaining, buffer.size());
    const auto count = ::read(source.get(), buffer.data(), static_cast<std::size_t>(wanted));
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      ::unlink(destination.c_str());
      throw std::runtime_error("capture changed while exporting the bounded snapshot");
    }
    std::size_t offset{};
    while (offset < static_cast<std::size_t>(count)) {
      const auto written =
          ::write(target.get(), buffer.data() + offset, static_cast<std::size_t>(count) - offset);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) {
        ::unlink(destination.c_str());
        throw std::runtime_error("cannot write complete capture export");
      }
      offset += static_cast<std::size_t>(written);
    }
    remaining -= static_cast<std::uint64_t>(count);
  }
  if (::fsync(target.get()) != 0) {
    const auto failure = errno;
    ::unlink(destination.c_str());
    throw std::system_error(failure, std::generic_category(), "cannot sync capture export");
  }
  output << "Exported Ethernet PCAPNG capture " << capture_id << " to " << destination
         << " bytes=" << metadata.st_size << '\n';
  return 0;
}

}  // namespace graphx
