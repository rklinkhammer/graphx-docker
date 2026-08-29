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
