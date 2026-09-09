#include "graphx/network.hpp"

#include <algorithm>

namespace graphx {

const NetworkDefinition& NetworkInfrastructureConfig::network(std::string_view id) const {
  const auto found =
      std::ranges::find_if(networks, [&](const auto& value) { return value.id == id; });
  if (found == networks.end()) throw std::out_of_range("unknown network '" + std::string(id) + "'");
  return *found;
}

const SwitchDefinition& NetworkInfrastructureConfig::network_switch(std::string_view id) const {
  const auto found =
      std::ranges::find_if(switches, [&](const auto& value) { return value.id == id; });
  if (found == switches.end()) throw std::out_of_range("unknown switch '" + std::string(id) + "'");
  return *found;
}

const RouterDefinition& NetworkInfrastructureConfig::router(std::string_view id) const {
  const auto found =
      std::ranges::find_if(routers, [&](const auto& value) { return value.id == id; });
  if (found == routers.end()) throw std::out_of_range("unknown router '" + std::string(id) + "'");
  return *found;
}

const EdgeNetworkPath& NetworkInfrastructureConfig::edge_path(std::string_view edge_id) const {
  const auto found =
      std::ranges::find_if(edge_paths, [&](const auto& value) { return value.edge_id == edge_id; });
  if (found == edge_paths.end())
    throw std::out_of_range("unknown edge network path '" + std::string(edge_id) + "'");
  return *found;
}

std::string_view to_string(NetworkDriver driver) noexcept {
  switch (driver) {
    case NetworkDriver::bridge:
      return "bridge";
    case NetworkDriver::macvlan:
      return "macvlan";
    case NetworkDriver::ipvlan:
      return "ipvlan";
  }
  return "unknown";
}

std::string_view to_string(NetworkProfile profile) noexcept {
  switch (profile) {
    case NetworkProfile::ethernet:
      return "ethernet";
    case NetworkProfile::macvlan:
      return "macvlan";
    case NetworkProfile::ipvlan_l2:
      return "ipvlan-l2";
    case NetworkProfile::ipvlan_l3:
      return "ipvlan-l3";
    case NetworkProfile::ipvlan_l3s:
      return "ipvlan-l3s";
  }
  return "unknown";
}

std::string_view to_string(AttachmentKind kind) noexcept {
  switch (kind) {
    case AttachmentKind::container_veth:
      return "container_veth";
    case AttachmentKind::namespace_veth:
      return "namespace_veth";
    case AttachmentKind::qemu_tap:
      return "qemu_tap";
    case AttachmentKind::external:
      return "external";
    case AttachmentKind::mirror:
      return "mirror";
  }
  return "unknown";
}

const NetworkProfileSemantics& profile_semantics(NetworkProfile profile) noexcept {
  static constexpr NetworkProfileSemantics ethernet{
      "endpoint", "dynamic", "ovs", "endpoint", "flood", "flood", "l2", "none", "separate"};
  static constexpr NetworkProfileSemantics macvlan{
      "endpoint", "dynamic", "ovs", "endpoint", "flood", "flood", "l2", "host", "separate"};
  static constexpr NetworkProfileSemantics ipvlan_l2{
      "shared-uplink", "suppressed", "ovs", "endpoint-shared-mac", "flood", "flood", "l2",
      "host",          "separate"};
  static constexpr NetworkProfileSemantics ipvlan_l3{
      "shared-uplink", "none", "route",    "suppressed", "suppressed",
      "suppressed",    "l3",   "endpoint", "separate"};
  static constexpr NetworkProfileSemantics ipvlan_l3s{
      "shared-uplink",       "none",       "source-validated",
      "suppressed",          "suppressed", "suppressed",
      "l3-source-validated", "endpoint",   "separate"};
  switch (profile) {
    case NetworkProfile::ethernet:
      return ethernet;
    case NetworkProfile::macvlan:
      return macvlan;
    case NetworkProfile::ipvlan_l2:
      return ipvlan_l2;
    case NetworkProfile::ipvlan_l3:
      return ipvlan_l3;
    case NetworkProfile::ipvlan_l3s:
      return ipvlan_l3s;
  }
  return ethernet;
}

std::string_view to_string(SwitchKind kind) noexcept {
  switch (kind) {
    case SwitchKind::openvswitch:
      return "openvswitch";
  }
  return "unknown";
}

std::string_view to_string(RouterKind kind) noexcept {
  switch (kind) {
    case RouterKind::linux_namespace:
      return "linux_namespace";
    case RouterKind::container:
      return "container";
  }
  return "unknown";
}

}  // namespace graphx
