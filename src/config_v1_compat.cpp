#include "config_internal.hpp"

namespace graphx::config_internal {

std::optional<std::string> interpret_v1_network(std::string_view driver, std::string_view mode,
                                                NetworkDefinition& network) {
  if (driver == "bridge")
    network.driver = NetworkDriver::bridge;
  else if (driver == "macvlan")
    network.driver = NetworkDriver::macvlan;
  else if (driver == "ipvlan")
    network.driver = NetworkDriver::ipvlan;
  else
    return "must be 'bridge', 'macvlan', or 'ipvlan'";
  network.mode = mode;
  return std::nullopt;
}

std::optional<std::string> validate_v1_network(const NetworkDefinition& network) {
  if ((network.driver == NetworkDriver::macvlan || network.driver == NetworkDriver::ipvlan) &&
      network.parent.empty())
    return "parent is required for macvlan and ipvlan";
  if (network.driver == NetworkDriver::ipvlan && network.mode != "l2" && network.mode != "l3" &&
      network.mode != "l3s")
    return "mode must be 'l2', 'l3', or 'l3s' for ipvlan";
  return std::nullopt;
}

}  // namespace graphx::config_internal
