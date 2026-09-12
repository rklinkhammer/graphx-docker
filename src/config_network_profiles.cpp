#include "config_internal.hpp"

namespace graphx::config_internal {

std::optional<std::string> interpret_v2_network(std::string_view profile,
                                                NetworkDefinition& network) {
  if (profile == "ethernet")
    network.profile = NetworkProfile::ethernet;
  else if (profile == "macvlan")
    network.profile = NetworkProfile::macvlan;
  else if (profile == "ipvlan-l2")
    network.profile = NetworkProfile::ipvlan_l2;
  else if (profile == "ipvlan-l3")
    network.profile = NetworkProfile::ipvlan_l3;
  else if (profile == "ipvlan-l3s")
    network.profile = NetworkProfile::ipvlan_l3s;
  else
    return "must be 'ethernet', 'macvlan', 'ipvlan-l2', 'ipvlan-l3', or 'ipvlan-l3s'";
  return std::nullopt;
}

std::optional<std::string> validate_v2_network(const NetworkDefinition& network) {
  if (network.profile != NetworkProfile::ethernet && network.uplink.empty())
    return "uplink is required for macvlan and ipvlan semantic profiles";
  return std::nullopt;
}

}  // namespace graphx::config_internal
