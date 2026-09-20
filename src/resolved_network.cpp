#include "config_document.hpp"

#include <algorithm>
#include <map>

namespace graphx::config_internal {
namespace {
const Array& entries(const Value& value, std::string_view key) {
  static const Array empty;
  return value.contains(key) ? value.at(key).array() : empty;
}
RouteDefinition route(const Value& value) {
  return {value.at("destination").text(), string_or(value, "via"), string_or(value, "device"),
          string_or(value, "install", "create") == "create"};
}
}  // namespace

NetworkInfrastructureConfig resolved_network(const Value& value) {
  NetworkInfrastructureConfig result;
  std::map<std::string, std::string> switches;
  for (const auto& item : value.at("networks").array()) {
    NetworkDefinition network;
    network.id = item.at("id").text();
    const std::map<std::string, NetworkProfile> profiles{
        {"ethernet", NetworkProfile::ethernet},
        {"macvlan", NetworkProfile::macvlan},
        {"ipvlan-l2", NetworkProfile::ipvlan_l2},
        {"ipvlan-l3", NetworkProfile::ipvlan_l3},
        {"ipvlan-l3s", NetworkProfile::ipvlan_l3s}};
    network.profile = profiles.at(item.at("profile").text());
    network.gateway = string_or(item, "gateway");
    network.external = false;
    for (const auto& subnet : entries(item, "subnets")) network.subnets.push_back(subnet.text());
    result.networks.push_back(std::move(network));
  }
  for (const auto& item : value.at("switches").array()) {
    SwitchDefinition sw;
    sw.id = item.at("name").text();
    if (!switches.emplace(item.at("id").text(), sw.id).second)
      throw std::invalid_argument("E_REFERENCE: duplicate resolved switch");
    for (const auto& p : item.at("ports").array()) {
      SwitchPortDefinition port;
      port.id = p.at("id").text();
      port.interface = p.at("interface").text();
      port.peer = string_or(p, "peer");
      if (p.contains("vlan")) {
        const auto& vlan = p.at("vlan");
        if (vlan.contains("access_tag"))
          port.vlan.access_tag = static_cast<std::uint16_t>(vlan.at("access_tag").integer());
        for (const auto& trunk : entries(vlan, "trunks"))
          port.vlan.trunks.push_back(static_cast<std::uint16_t>(trunk.integer()));
      }
      sw.ports.push_back(std::move(port));
    }
    if (item.contains("mirror")) {
      const auto& m = item.at("mirror");
      sw.mirror = MirrorDefinition{m.at("id").text(), m.at("output_port").text(),
                                   bool_or(m, "select_all", true)};
    }
    result.switches.push_back(std::move(sw));
  }
  for (const auto& item : value.at("routers").array()) {
    RouterDefinition router;
    router.id = item.at("id").text();
    router.namespace_name = item.at("namespace").text();
    router.forwarding = bool_or(item, "forwarding", true);
    for (const auto& i : item.at("interfaces").array())
      router.interfaces.push_back({i.at("id").text(), i.at("network").text(),
                                   i.at("address").text(), i.at("device").text(),
                                   i.at("peer").text(), switches.at(i.at("switch").text())});
    for (const auto& r : entries(item, "routes")) router.routes.push_back(route(r));
    // Policy order is authored order, including overlapping allow/deny rules.
    for (const auto& p : entries(item, "policies"))
      router.policies.push_back({p.at("id").text(), p.at("source").text(),
                                 p.at("destination").text(), p.at("action").text()});
    result.routers.push_back(std::move(router));
  }
  for (const auto& item : value.at("attachments").array()) {
    AttachmentDefinition a;
    a.id = item.at("id").text();
    const std::map<std::string, AttachmentKind> kinds{
        {"external", AttachmentKind::external},
        {"container_veth", AttachmentKind::container_veth},
        {"namespace_veth", AttachmentKind::namespace_veth},
        {"qemu_tap", AttachmentKind::qemu_tap},
        {"mirror", AttachmentKind::mirror}};
    a.kind = kinds.at(item.at("kind").text());
    a.owner = item.at("owner").text();
    a.network = string_or(item, "network");
    a.address = string_or(item, "address");
    a.mac = string_or(item, "mac");
    a.interface = string_or(item, "interface");
    a.peer = string_or(item, "peer");
    if (item.contains("switch")) a.network_switch = switches.at(item.at("switch").text());
    a.mirror_container = string_or(item, "delivery", "host") == "container";
    a.mtu = static_cast<std::uint32_t>(integer_or(item, "mtu", 1500));
    a.tap_uid = static_cast<std::uint32_t>(integer_or(item, "tap_uid", 0));
    a.tap_gid = static_cast<std::uint32_t>(integer_or(item, "tap_gid", 0));
    for (const auto& r : entries(item, "routes")) a.routes.push_back(route(r));
    for (const auto& alias : entries(item, "aliases")) a.aliases.push_back(alias.text());
    // Diagnostic namespaces use the same namespace owner as router namespaces.
    // Router interfaces are already expanded by normalization; never expand them twice.
    if (a.kind == AttachmentKind::namespace_veth && item.contains("namespace")) {
      const auto found = std::ranges::find(result.routers, a.owner, &RouterDefinition::id);
      if (found == result.routers.end()) {
        RouterDefinition ns;
        ns.id = a.owner;
        ns.namespace_name = item.at("namespace").text();
        ns.forwarding = false;
        result.routers.push_back(std::move(ns));
      } else if (found->namespace_name != item.at("namespace").text()) {
        throw std::invalid_argument("E_REFERENCE: conflicting namespace ownership");
      }
    }
    result.attachments.push_back(std::move(a));
  }
  for (const auto& item : value.at("captures").array()) {
    NetworkCaptureDefinition capture;
    capture.id = item.at("id").text();
    capture.attachment = item.at("attachment").text();
    capture.directory = item.at("directory").text();
    capture.snaplen = static_cast<std::uint32_t>(item.at("snaplen").integer());
    capture.max_file_bytes = static_cast<std::uint64_t>(item.at("max_file_bytes").integer());
    capture.max_files = static_cast<std::uint32_t>(item.at("max_files").integer());
    capture.rotation_seconds = static_cast<std::uint32_t>(item.at("rotation_seconds").integer());
    capture.retention_seconds = static_cast<std::uint32_t>(item.at("retention_seconds").integer());
    result.captures.push_back(std::move(capture));
  }
  for (const auto& [id, hops] : value.at("edge_paths").object()) {
    EdgeNetworkPath path;
    path.edge_id = id;
    for (const auto& hop : hops.array()) path.hops.push_back(hop.text());
    result.edge_paths.push_back(std::move(path));
  }
  return result;
}
}  // namespace graphx::config_internal
