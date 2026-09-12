#include "config_internal.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace graphx::config_internal {

VlanMetadata ConfigParser::parse_vlan(const YAML::Node& value, const std::string& path) {
  VlanMetadata vlan;
  if (!value) return vlan;
  if (!require_map(value, path)) return vlan;
  strict_keys(value, path, {"access_tag", "trunks"});
  if (value["access_tag"]) {
    const auto tag = unsigned_value(value["access_tag"], path + ".access_tag");
    if (tag == 0 || tag > 4094)
      error(path + ".access_tag", "must be between 1 and 4094");
    else
      vlan.access_tag = static_cast<std::uint16_t>(tag);
  }
  const auto trunks = value["trunks"];
  if (trunks && require_sequence(trunks, path + ".trunks")) {
    for (std::size_t index = 0; index < trunks.size(); ++index) {
      const auto tag =
          unsigned_value(trunks[index], path + ".trunks[" + std::to_string(index) + "]");
      if (tag == 0 || tag > 4094)
        error(path + ".trunks[" + std::to_string(index) + "]", "must be between 1 and 4094");
      else
        vlan.trunks.push_back(static_cast<std::uint16_t>(tag));
    }
  }
  return vlan;
}

void ConfigParser::parse_network_infrastructure(const YAML::Node& infrastructure,
                                                GraphConfig& config) {
  if (!infrastructure) return;
  if (!require_map(infrastructure, "network")) return;
  strict_keys(
      infrastructure, "network",
      {"networks", "switches", "routers", "attachments", "edge_paths", "captures", "faults"});
  parse_networks(infrastructure["networks"], config);
  parse_switches(infrastructure["switches"], config);
  parse_routers(infrastructure["routers"], config);
  parse_attachments(infrastructure["attachments"], config);
  parse_edge_paths(infrastructure["edge_paths"], config);
  parse_network_captures(infrastructure["captures"], config);
  parse_network_faults(infrastructure["faults"], config);
}

void ConfigParser::parse_networks(const YAML::Node& values, GraphConfig& config) {
  if (!values) return;
  if (!require_sequence(values, "network.networks")) return;
  std::unordered_set<std::string> ids;
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto path = "network.networks[" + std::to_string(index) + "]";
    const auto value = values[index];
    if (!require_map(value, path)) continue;
    NetworkDefinition network;
    network.id = text(value["id"], path + ".id", 64);
    identifier(network.id, path + ".id");
    if (!network.id.empty() && !ids.insert(network.id).second)
      error(path + ".id", "duplicate network id '" + network.id + "'");
    strict_keys(value, path, {"id", "profile", "subnets", "gateway", "uplink", "external"});
    const auto profile = text(value["profile"], path + ".profile", 16);
    if (const auto issue = config_internal::interpret_network_profile(profile, network))
      error(path + ".profile", *issue);
    if (value["uplink"]) network.uplink = text(value["uplink"], path + ".uplink", 15);
    const bool layer_three = network.profile == NetworkProfile::ipvlan_l3 ||
                             network.profile == NetworkProfile::ipvlan_l3s;
    if (value["subnets"] && require_sequence(value["subnets"], path + ".subnets")) {
      if (value["subnets"].size() == 0 || value["subnets"].size() > 16)
        error(path + ".subnets", "must contain between 1 and 16 subnets");
      for (std::size_t subnet_index = 0; subnet_index < value["subnets"].size(); ++subnet_index)
        network.subnets.push_back(text(value["subnets"][subnet_index],
                                       path + ".subnets[" + std::to_string(subnet_index) + "]",
                                       43));
    } else
      error(path + ".subnets", "is required");
    if (!layer_three && network.subnets.size() > 1)
      error(path + ".subnets", "multiple subnets require ipvlan l3 or l3s mode");
    if (value["gateway"])
      network.gateway = text(value["gateway"], path + ".gateway", 39);
    else if (!layer_three)
      error(path + ".gateway", "is required except for ipvlan l3/l3s");
    network.external = strict_bool_value(value["external"], path + ".external", true);
    if (const auto issue = config_internal::validate_network_profile(network)) {
      error(path + ".uplink", issue->substr(issue->find(' ') + 1));
    }
    config.network_infrastructure.networks.push_back(std::move(network));
  }
}

void ConfigParser::parse_switches(const YAML::Node& values, GraphConfig& config) {
  if (!values) return;
  if (!require_sequence(values, "network.switches")) return;
  std::unordered_set<std::string> ids;
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto path = "network.switches[" + std::to_string(index) + "]";
    const auto value = values[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path, {"id", "kind", "datapath", "ports", "mirror"});
    SwitchDefinition network_switch;
    network_switch.id = text(value["id"], path + ".id", 15);
    identifier(network_switch.id, path + ".id");
    if (!network_switch.id.empty() && !ids.insert(network_switch.id).second)
      error(path + ".id", "duplicate switch id '" + network_switch.id + "'");
    const auto kind = text(value["kind"], path + ".kind", 32);
    if (kind != "openvswitch") error(path + ".kind", "must be 'openvswitch'");
    if (value["datapath"])
      network_switch.datapath = text(value["datapath"], path + ".datapath", 16);
    if (network_switch.datapath != "system" && network_switch.datapath != "netdev")
      error(path + ".datapath", "must be 'system' or 'netdev'");
    if (network_switch.datapath != "system")
      error(path + ".datapath", "must use the Open vSwitch system datapath");
    const auto ports = value["ports"];
    if (ports && require_sequence(ports, path + ".ports")) {
      std::unordered_set<std::string> port_ids;
      for (std::size_t port_index = 0; port_index < ports.size(); ++port_index) {
        const auto port_path = path + ".ports[" + std::to_string(port_index) + "]";
        const auto port_value = ports[port_index];
        if (!require_map(port_value, port_path)) continue;
        strict_keys(port_value, port_path, {"id", "interface", "peer", "vlan"});
        SwitchPortDefinition port;
        port.id = text(port_value["id"], port_path + ".id", 64);
        identifier(port.id, port_path + ".id");
        if (!port.id.empty() && !port_ids.insert(port.id).second)
          error(port_path + ".id", "duplicate switch port id '" + port.id + "'");
        port.interface = text(port_value["interface"], port_path + ".interface", 15);
        if (port_value["peer"]) port.peer = text(port_value["peer"], port_path + ".peer", 15);
        port.vlan = parse_vlan(port_value["vlan"], port_path + ".vlan");
        network_switch.ports.push_back(std::move(port));
      }
    }
    const auto mirror = value["mirror"];
    if (mirror && require_map(mirror, path + ".mirror")) {
      strict_keys(mirror, path + ".mirror", {"id", "output_port", "select_all"});
      MirrorDefinition definition;
      definition.id = text(mirror["id"], path + ".mirror.id", 64);
      identifier(definition.id, path + ".mirror.id");
      definition.output_port = text(mirror["output_port"], path + ".mirror.output_port", 64);
      definition.select_all = bool_value(mirror["select_all"], path + ".mirror.select_all", true);
      network_switch.mirror = std::move(definition);
    }
    config.network_infrastructure.switches.push_back(std::move(network_switch));
  }
}

void ConfigParser::parse_routers(const YAML::Node& values, GraphConfig& config) {
  if (!values) return;
  if (!require_sequence(values, "network.routers")) return;
  std::unordered_set<std::string> ids;
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto path = "network.routers[" + std::to_string(index) + "]";
    const auto value = values[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path,
                {"id", "kind", "namespace", "forwarding", "interfaces", "routes", "policies"});
    RouterDefinition router;
    router.id = text(value["id"], path + ".id", 64);
    identifier(router.id, path + ".id");
    if (!router.id.empty() && !ids.insert(router.id).second)
      error(path + ".id", "duplicate router id '" + router.id + "'");
    const auto kind = text(value["kind"], path + ".kind", 32);
    if (kind == "linux_namespace")
      router.kind = RouterKind::linux_namespace;
    else if (kind == "container")
      router.kind = RouterKind::container;
    else
      error(path + ".kind", "must be 'linux_namespace' or 'container'");
    if (value["namespace"])
      router.namespace_name = text(value["namespace"], path + ".namespace", 64);
    if (router.kind == RouterKind::linux_namespace && router.namespace_name.empty())
      error(path + ".namespace", "is required for a Linux namespace router");
    router.forwarding = bool_value(value["forwarding"], path + ".forwarding", true);
    const auto interfaces = value["interfaces"];
    if (!require_sequence(interfaces, path + ".interfaces")) continue;
    for (std::size_t interface_index = 0; interface_index < interfaces.size(); ++interface_index) {
      const auto interface_path = path + ".interfaces[" + std::to_string(interface_index) + "]";
      const auto interface_value = interfaces[interface_index];
      if (!require_map(interface_value, interface_path)) continue;
      strict_keys(interface_value, interface_path,
                  {"id", "network", "address", "device", "peer", "switch"});
      RouterInterfaceDefinition interface;
      interface.id = text(interface_value["id"], interface_path + ".id", 64);
      identifier(interface.id, interface_path + ".id");
      interface.network = text(interface_value["network"], interface_path + ".network", 64);
      interface.address = text(interface_value["address"], interface_path + ".address", 43);
      interface.device = text(interface_value["device"], interface_path + ".device", 15);
      interface.peer = text(interface_value["peer"], interface_path + ".peer", 15);
      interface.network_switch = text(interface_value["switch"], interface_path + ".switch", 15);
      router.interfaces.push_back(std::move(interface));
    }
    if (router.interfaces.size() < 2)
      error(path + ".interfaces", "must contain at least two interfaces");
    const auto routes = value["routes"];
    if (routes && require_sequence(routes, path + ".routes")) {
      for (std::size_t route_index = 0; route_index < routes.size(); ++route_index) {
        const auto route_path = path + ".routes[" + std::to_string(route_index) + "]";
        const auto route_value = routes[route_index];
        if (!require_map(route_value, route_path)) continue;
        strict_keys(route_value, route_path, {"destination", "via", "device", "install"});
        RouteDefinition route;
        route.destination = text(route_value["destination"], route_path + ".destination", 43);
        if (route_value["via"]) route.via = text(route_value["via"], route_path + ".via", 39);
        if (route_value["device"])
          route.device = text(route_value["device"], route_path + ".device", 15);
        if (route_value["install"]) {
          const auto install = text(route_value["install"], route_path + ".install", 16);
          if (install == "manual")
            route.install_on_create = false;
          else if (install != "create")
            error(route_path + ".install", "must be 'create' or 'manual'");
        }
        router.routes.push_back(std::move(route));
      }
    }
    const auto policies = value["policies"];
    if (policies && require_sequence(policies, path + ".policies")) {
      for (std::size_t policy_index = 0; policy_index < policies.size(); ++policy_index) {
        const auto policy_path = path + ".policies[" + std::to_string(policy_index) + "]";
        const auto policy_value = policies[policy_index];
        if (!require_map(policy_value, policy_path)) continue;
        strict_keys(policy_value, policy_path, {"id", "source", "destination", "action"});
        PolicyDefinition policy;
        policy.id = text(policy_value["id"], policy_path + ".id", 64);
        identifier(policy.id, policy_path + ".id");
        if (policy_value["source"])
          policy.source = text(policy_value["source"], policy_path + ".source", 43);
        if (policy_value["destination"])
          policy.destination = text(policy_value["destination"], policy_path + ".destination", 43);
        if (policy_value["action"])
          policy.action = text(policy_value["action"], policy_path + ".action", 16);
        if (policy.action != "accept" && policy.action != "drop")
          error(policy_path + ".action", "must be 'accept' or 'drop'");
        router.policies.push_back(std::move(policy));
      }
    }
    config.network_infrastructure.routers.push_back(std::move(router));
  }
}

void ConfigParser::parse_attachments(const YAML::Node& values, GraphConfig& config) {
  if (!values) return;
  if (!require_sequence(values, "network.attachments")) return;
  if (values.size() > kMaxNodes * kMaxPortsPerNode)
    error("network.attachments", "exceeds maximum attachment count 262144");
  std::unordered_set<std::string> ids;
  const auto count = std::min<std::size_t>(values.size(), kMaxNodes * kMaxPortsPerNode);
  for (std::size_t index = 0; index < count; ++index) {
    const auto path = "network.attachments[" + std::to_string(index) + "]";
    const auto value = values[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path,
                {"id", "kind", "owner", "network", "address", "mac", "interface", "peer", "switch",
                 "mtu", "tap_uid", "tap_gid", "routes"});
    AttachmentDefinition attachment;
    attachment.id = text(value["id"], path + ".id", 64);
    identifier(attachment.id, path + ".id");
    if (!attachment.id.empty() && !ids.insert(attachment.id).second)
      error(path + ".id", "duplicate attachment id '" + attachment.id + "'");
    const auto kind = text(value["kind"], path + ".kind", 32);
    if (kind == "container_veth")
      attachment.kind = AttachmentKind::container_veth;
    else if (kind == "namespace_veth")
      attachment.kind = AttachmentKind::namespace_veth;
    else if (kind == "qemu_tap")
      attachment.kind = AttachmentKind::qemu_tap;
    else if (kind == "external")
      attachment.kind = AttachmentKind::external;
    else if (kind == "mirror")
      attachment.kind = AttachmentKind::mirror;
    else
      error(path + ".kind",
            "must be 'container_veth', 'namespace_veth', 'qemu_tap', 'external', or 'mirror'");
    attachment.owner = text(value["owner"], path + ".owner", 64);
    identifier(attachment.owner, path + ".owner");
    if (value["network"]) attachment.network = text(value["network"], path + ".network", 64);
    if (value["address"]) attachment.address = text(value["address"], path + ".address", 43);
    if (value["mac"]) attachment.mac = text(value["mac"], path + ".mac", 17);
    if (!attachment.mac.empty() && !std::regex_match(attachment.mac, kMacAddress))
      error(path + ".mac", "must be a six-octet MAC address");
    if (value["interface"])
      attachment.interface = text(value["interface"], path + ".interface", 15);
    if (value["peer"]) attachment.peer = text(value["peer"], path + ".peer", 15);
    if (value["switch"]) attachment.network_switch = text(value["switch"], path + ".switch", 15);
    if (value["mtu"]) {
      const auto mtu = unsigned_value(value["mtu"], path + ".mtu");
      if (mtu < 576 || mtu > 9216)
        error(path + ".mtu", "must be between 576 and 9216");
      else
        attachment.mtu = static_cast<std::uint32_t>(mtu);
    }
    if (value["tap_uid"]) {
      const auto uid = unsigned_value(value["tap_uid"], path + ".tap_uid");
      if (uid == 0 || uid >= UINT32_MAX)
        error(path + ".tap_uid", "must identify a non-root UID below 4294967295");
      else
        attachment.tap_uid = static_cast<std::uint32_t>(uid);
    }
    if (value["tap_gid"]) {
      const auto gid = unsigned_value(value["tap_gid"], path + ".tap_gid");
      if (gid == 0 || gid >= UINT32_MAX)
        error(path + ".tap_gid", "must identify a non-root GID below 4294967295");
      else
        attachment.tap_gid = static_cast<std::uint32_t>(gid);
    }
    const auto routes = value["routes"];
    if (routes && require_sequence(routes, path + ".routes")) {
      for (std::size_t route_index = 0; route_index < routes.size(); ++route_index) {
        const auto route_path = path + ".routes[" + std::to_string(route_index) + "]";
        const auto route_value = routes[route_index];
        if (!require_map(route_value, route_path)) continue;
        strict_keys(route_value, route_path, {"destination", "via"});
        RouteDefinition route;
        route.destination = text(route_value["destination"], route_path + ".destination", 43);
        if (route_value["via"]) route.via = text(route_value["via"], route_path + ".via", 39);
        attachment.routes.push_back(std::move(route));
      }
    }
    if (attachment.kind == AttachmentKind::mirror) {
      if (!attachment.network.empty() || !attachment.address.empty() || !attachment.mac.empty() ||
          !attachment.peer.empty())
        error(path, "mirror attachments allow only id, kind, owner, interface, and switch");
      if (attachment.interface.empty()) error(path + ".interface", "is required for a mirror");
      if (attachment.network_switch.empty()) error(path + ".switch", "is required for a mirror");
    } else if (attachment.network.empty()) {
      error(path + ".network", "is required except for mirror attachments");
    }
    if (attachment.kind == AttachmentKind::container_veth &&
        (attachment.interface.empty() || attachment.peer.empty() ||
         attachment.network_switch.empty()))
      error(path, "container_veth requires interface, peer, and switch");
    if (attachment.kind == AttachmentKind::namespace_veth &&
        (attachment.interface.empty() || attachment.peer.empty() ||
         attachment.network_switch.empty() || attachment.address.empty()))
      error(path, "namespace_veth requires address, interface, peer, and switch");
    if (attachment.kind == AttachmentKind::qemu_tap &&
        (attachment.interface.empty() || attachment.network_switch.empty() ||
         attachment.address.empty() || attachment.tap_uid == 0 || attachment.tap_gid == 0))
      error(path, "qemu_tap requires address, interface, switch, tap_uid, and tap_gid");
    if (attachment.kind == AttachmentKind::qemu_tap &&
        (!attachment.peer.empty() || !attachment.routes.empty()))
      error(path, "qemu_tap does not allow peer or host-installed routes");
    if (attachment.kind != AttachmentKind::qemu_tap &&
        (attachment.tap_uid != 0 || attachment.tap_gid != 0))
      error(path, "tap_uid and tap_gid are allowed only for qemu_tap");
    config.network_infrastructure.attachments.push_back(std::move(attachment));
  }
}

void ConfigParser::parse_edge_paths(const YAML::Node& paths, GraphConfig& config) {
  if (!paths) return;
  if (!require_map(paths, "network.edge_paths")) return;
  for (const auto& entry : paths) {
    if (!entry.first.IsScalar()) {
      error("network.edge_paths", "contains a non-scalar edge id");
      continue;
    }
    EdgeNetworkPath path;
    path.edge_id = entry.first.Scalar();
    const auto item_path = "network.edge_paths." + path.edge_id;
    if (!require_sequence(entry.second, item_path)) continue;
    if (entry.second.size() < 2) error(item_path, "must contain at least two hops");
    for (std::size_t index = 0; index < entry.second.size(); ++index)
      path.hops.push_back(
          text(entry.second[index], item_path + "[" + std::to_string(index) + "]", 64));
    config.network_infrastructure.edge_paths.push_back(std::move(path));
  }
}

void ConfigParser::parse_network_captures(const YAML::Node& values, GraphConfig& config) {
  if (!values) return;
  if (!require_sequence(values, "network.captures")) return;
  if (values.size() > 64) error("network.captures", "exceeds maximum capture count 64");
  std::unordered_set<std::string> ids;
  const auto count = std::min<std::size_t>(values.size(), 64);
  for (std::size_t index = 0; index < count; ++index) {
    const auto path = "network.captures[" + std::to_string(index) + "]";
    const auto value = values[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path,
                {"id", "attachment", "directory", "snaplen", "max_file_bytes", "max_files",
                 "rotation_seconds", "retention_seconds"});
    NetworkCaptureDefinition capture;
    capture.id = text(value["id"], path + ".id", 64);
    identifier(capture.id, path + ".id");
    if (!capture.id.empty() && !ids.insert(capture.id).second)
      error(path + ".id", "duplicate network capture id '" + capture.id + "'");
    capture.attachment = text(value["attachment"], path + ".attachment", 64);
    identifier(capture.attachment, path + ".attachment");
    capture.directory = text(value["directory"], path + ".directory", 1024);
    if (!capture.directory.starts_with("/var/lib/graphx/captures/") ||
        capture.directory.find("..") != std::string::npos)
      error(path + ".directory", "must be beneath /var/lib/graphx/captures without '..'");
    if (value["snaplen"]) capture.snaplen = unsigned_value(value["snaplen"], path + ".snaplen");
    if (capture.snaplen < 256 || capture.snaplen > 262144)
      error(path + ".snaplen", "must be between 256 and 262144");
    if (value["max_file_bytes"])
      capture.max_file_bytes = unsigned_64_value(value["max_file_bytes"], path + ".max_file_bytes");
    if (capture.max_file_bytes < 65536 || capture.max_file_bytes > 4ULL * 1024 * 1024 * 1024)
      error(path + ".max_file_bytes", "must be between 65536 and 4294967296");
    if (value["max_files"])
      capture.max_files = unsigned_value(value["max_files"], path + ".max_files");
    if (capture.max_files == 0 || capture.max_files > 1024)
      error(path + ".max_files", "must be between 1 and 1024");
    if (value["rotation_seconds"])
      capture.rotation_seconds =
          unsigned_value(value["rotation_seconds"], path + ".rotation_seconds");
    if (capture.rotation_seconds == 0 || capture.rotation_seconds > 86400)
      error(path + ".rotation_seconds", "must be between 1 and 86400");
    if (value["retention_seconds"])
      capture.retention_seconds =
          unsigned_value(value["retention_seconds"], path + ".retention_seconds");
    if (capture.retention_seconds < capture.rotation_seconds ||
        capture.retention_seconds > 31536000)
      error(path + ".retention_seconds",
            "must be at least rotation_seconds and no more than 31536000");
    if (static_cast<std::uint64_t>(capture.max_files) * capture.rotation_seconds >
        capture.retention_seconds)
      error(path, "max_files times rotation_seconds must not exceed retention_seconds");
    config.network_infrastructure.captures.push_back(std::move(capture));
  }
}

void ConfigParser::parse_network_faults(const YAML::Node& values, GraphConfig& config) {
  if (!values) return;
  if (!require_sequence(values, "network.faults")) return;
  if (values.size() > 64) error("network.faults", "exceeds maximum fault count 64");
  std::unordered_set<std::string> ids;
  const auto count = std::min<std::size_t>(values.size(), 64);
  for (std::size_t index = 0; index < count; ++index) {
    const auto path = "network.faults[" + std::to_string(index) + "]";
    const auto value = values[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path,
                {"id", "attachment", "delay_ms", "jitter_ms", "loss_percent", "rate_kbit",
                 "duration_seconds"});
    NetworkFaultDefinition fault;
    fault.id = text(value["id"], path + ".id", 64);
    identifier(fault.id, path + ".id");
    if (!fault.id.empty() && !ids.insert(fault.id).second)
      error(path + ".id", "duplicate network fault id '" + fault.id + "'");
    fault.attachment = text(value["attachment"], path + ".attachment", 64);
    identifier(fault.attachment, path + ".attachment");
    if (value["delay_ms"]) fault.delay_ms = unsigned_value(value["delay_ms"], path + ".delay_ms");
    if (value["jitter_ms"])
      fault.jitter_ms = unsigned_value(value["jitter_ms"], path + ".jitter_ms");
    if (fault.delay_ms > 600000 || fault.jitter_ms > fault.delay_ms)
      error(path, "delay_ms must be at most 600000 and jitter_ms must not exceed delay_ms");
    if (value["loss_percent"])
      fault.loss_percent = double_value(value["loss_percent"], path + ".loss_percent", 0.0);
    if (fault.loss_percent < 0.0 || fault.loss_percent > 100.0)
      error(path + ".loss_percent", "must be between 0 and 100");
    if (value["rate_kbit"])
      fault.rate_kbit = unsigned_value(value["rate_kbit"], path + ".rate_kbit");
    if (fault.rate_kbit > 100000000) error(path + ".rate_kbit", "must be no more than 100000000");
    if (value["duration_seconds"])
      fault.duration_seconds =
          unsigned_value(value["duration_seconds"], path + ".duration_seconds");
    if (fault.duration_seconds == 0 || fault.duration_seconds > 86400)
      error(path + ".duration_seconds", "must be between 1 and 86400");
    if (fault.delay_ms == 0 && fault.loss_percent == 0.0 && fault.rate_kbit == 0)
      error(path, "must declare delay_ms, loss_percent, or rate_kbit");
    config.network_infrastructure.faults.push_back(std::move(fault));
  }
}

}  // namespace graphx::config_internal
