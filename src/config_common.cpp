#include "config_internal.hpp"

namespace graphx::config_internal {

std::optional<Dialect> dialect_for(std::uint32_t version) noexcept {
  if (version == 1) return Dialect::v1_compat;
  if (version == 2) return Dialect::v2;
  return std::nullopt;
}

bool is_v1(Dialect dialect) noexcept { return dialect == Dialect::v1_compat; }

bool is_layer_three(const NetworkDefinition& network, Dialect dialect) noexcept {
  if (is_v1(dialect))
    return network.driver == NetworkDriver::ipvlan &&
           (network.mode == "l3" || network.mode == "l3s");
  return network.profile == NetworkProfile::ipvlan_l3 ||
         network.profile == NetworkProfile::ipvlan_l3s;
}

}  // namespace graphx::config_internal
