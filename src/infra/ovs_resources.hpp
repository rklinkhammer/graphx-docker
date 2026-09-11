#pragma once

#include "infra/ownership_state.hpp"

#include <string>
#include <string_view>

namespace graphx::infra::detail {

std::string ovs_get(const std::string& bridge, const std::string& column);
std::string ovs_get(const std::string& table, const std::string& record, const std::string& column);
std::string ovs_find_uuid(const std::string& table, const std::string& column,
                          const std::string& value);
bool bridge_exists(const std::string& name);
std::string planned_create(const SwitchDefinition& network_switch, std::string_view graph_id,
                           std::string_view token, std::string_view hash);
bool bridge_owned(const OwnedResourceIdentity& bridge, const OwnershipState& state);
bool bridge_complete_set_owned(const OwnedResourceIdentity& bridge, const OwnershipState& state);
bool delete_owned_bridge(const OwnedResourceIdentity& bridge, const OwnershipState& state);
void configure_endpoint_vlan(const ExpectedEndpoint& endpoint);
bool endpoint_vlan_matches(const ExpectedEndpoint& expected, const OwnedResourceIdentity& endpoint);
std::string address_host(std::string value);
void install_profile_flows(const GraphConfig& config, const OwnershipState& state);

}  // namespace graphx::infra::detail
