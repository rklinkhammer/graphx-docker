#include "infra/ovs_resources.hpp"

#include "infra/command_runner.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace graphx::infra::detail {
namespace {

int run(const std::vector<std::string>& arguments, std::string* captured = nullptr) {
  CommandOptions options;
  options.arguments = arguments;
  options.capture_output = captured != nullptr;
  auto result = run_command(options);
  if (captured) *captured = std::move(result.output);
  if (result.output_truncated) throw std::runtime_error("command output exceeded safety limit");
  return result.status;
}

}  // namespace

std::string ovs_get(const std::string& bridge, const std::string& column) {
  std::string value;
  if (run({"ovs-vsctl", "get", "Bridge", bridge, column}, &value) != 0) return {};
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  return value;
}

std::string ovs_get(const std::string& table, const std::string& record,
                    const std::string& column) {
  std::string value;
  if (run({"ovs-vsctl", "get", table, record, column}, &value) != 0) return {};
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  return value;
}

std::vector<std::string> lines(std::string value) {
  std::vector<std::string> result;
  std::istringstream input(value);
  for (std::string line; std::getline(input, line);)
    if (!line.empty()) result.push_back(std::move(line));
  return result;
}

std::string ovs_find_uuid(const std::string& table, const std::string& column,
                          const std::string& value) {
  std::string found;
  if (run({"ovs-vsctl", "--data=bare", "--no-heading", "--columns=_uuid", "find", table,
           column + "=" + value},
          &found) != 0)
    return {};
  const auto matches = lines(found);
  return matches.size() == 1 ? matches.front() : std::string{};
}

bool bridge_exists(const std::string& name) { return run({"ovs-vsctl", "br-exists", name}) == 0; }

std::string planned_create(const SwitchDefinition& network_switch, std::string_view graph_id,
                           std::string_view token, std::string_view hash) {
  return "ovs-vsctl -- add-br " + network_switch.id + " -- set Bridge " + network_switch.id +
         " datapath_type=system external_ids:graphx_owner=" + std::string(token) +
         " external_ids:graphx_config_hash=" + std::string(hash) +
         " external_ids:graphx_graph=" + std::string(graph_id);
}

bool bridge_owned(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  const auto internal_port =
      bridge.secondary_id.empty() ? ovs_get("Port", bridge.name, "_uuid") : bridge.secondary_id;
  return !internal_port.empty() && bridge_exists(bridge.name) &&
         stable_identity_matches(bridge, bridge_identity(bridge.name, ovs_get(bridge.name, "_uuid"),
                                                         ovs_get("Port", bridge.name, "_uuid"))) &&
         ovs_get(bridge.name, "external_ids:graphx_owner") == state.owner_token &&
         ovs_get(bridge.name, "external_ids:graphx_config_hash") == state.config_hash &&
         ovs_get(bridge.name, "external_ids:graphx_graph") == state.graph_id;
}

std::unordered_set<std::string> ovs_uuid_set(std::string value) {
  std::unordered_set<std::string> result;
  std::string token;
  const auto flush = [&] {
    if (!token.empty()) result.insert(std::exchange(token, {}));
  };
  for (const auto character : value) {
    if (std::isxdigit(static_cast<unsigned char>(character)) || character == '-')
      token.push_back(character);
    else
      flush();
  }
  flush();
  return result;
}

bool bridge_complete_set_owned(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  if (!bridge_owned(bridge, state)) return false;
  const auto internal_port =
      bridge.secondary_id.empty() ? ovs_get("Port", bridge.name, "_uuid") : bridge.secondary_id;
  std::unordered_set<std::string> expected{internal_port};
  for (const auto& endpoint : state.endpoints) {
    const auto definition =
        std::find_if(state.expected_endpoints.begin(), state.expected_endpoints.end(),
                     [&](const auto& candidate) { return candidate.id == endpoint.attachment_id; });
    if (definition == state.expected_endpoints.end()) return false;
    if (definition->network_switch == bridge.name &&
        ovs_get("Port", endpoint.stable_id, "_uuid") == endpoint.stable_id)
      expected.insert(endpoint.stable_id);
  }
  return ovs_uuid_set(ovs_get("Bridge", bridge.stable_id, "ports")) == expected;
}

bool delete_owned_bridge(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  const auto internal_port =
      bridge.secondary_id.empty() ? ovs_get("Port", bridge.name, "_uuid") : bridge.secondary_id;
  if (internal_port.empty() || ovs_get("Port", bridge.name, "_uuid") != internal_port ||
      ovs_get("Bridge", bridge.stable_id, "ports") != "[" + internal_port + "]")
    return false;
  return run({"ovs-vsctl",
              "--timeout=2",
              "--",
              "wait-until",
              "Bridge",
              bridge.stable_id,
              "external_ids:graphx_owner=" + state.owner_token,
              "external_ids:graphx_config_hash=" + state.config_hash,
              "external_ids:graphx_graph=" + state.graph_id,
              "ports=[" + internal_port + "]",
              "--",
              "remove",
              "Open_vSwitch",
              ".",
              "bridges",
              bridge.stable_id,
              "--",
              "destroy",
              "Bridge",
              bridge.stable_id}) == 0;
}

std::string comma_join(const std::vector<std::uint16_t>& values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) output << ',';
    output << values[index];
  }
  return output.str();
}

void configure_endpoint_vlan(const ExpectedEndpoint& endpoint) {
  if (endpoint.vlan.access_tag && run({"ovs-vsctl", "set", "Port", endpoint.host_interface,
                                       "tag=" + std::to_string(*endpoint.vlan.access_tag)}) != 0)
    throw std::runtime_error("cannot configure OVS access VLAN for " + endpoint.id);
  if (!endpoint.vlan.trunks.empty() && run({"ovs-vsctl", "set", "Port", endpoint.host_interface,
                                            "trunks=" + comma_join(endpoint.vlan.trunks)}) != 0)
    throw std::runtime_error("cannot configure OVS trunk VLANs for " + endpoint.id);
}

bool endpoint_vlan_matches(const ExpectedEndpoint& expected,
                           const OwnedResourceIdentity& endpoint) {
  if (expected.vlan.access_tag &&
      ovs_get("Port", endpoint.stable_id, "tag") != std::to_string(*expected.vlan.access_tag))
    return false;
  if (!expected.vlan.trunks.empty()) {
    const auto observed = ovs_uuid_set(ovs_get("Port", endpoint.stable_id, "trunks"));
    std::unordered_set<std::string> wanted;
    for (const auto value : expected.vlan.trunks) wanted.insert(std::to_string(value));
    if (observed != wanted) return false;
  }
  return true;
}

std::string address_host(std::string value) {
  const auto slash = value.find('/');
  if (slash != std::string::npos) value.resize(slash);
  return value;
}

void install_profile_flows(const GraphConfig& config, const OwnershipState& state) {
  const auto cookie = "0x" + state.owner_token.substr(0, 16);
  for (const auto& network : config.network_infrastructure.networks) {
    if (!network.profile || (*network.profile != NetworkProfile::ipvlan_l2 &&
                             *network.profile != NetworkProfile::ipvlan_l3 &&
                             *network.profile != NetworkProfile::ipvlan_l3s))
      continue;
    std::unordered_set<std::string> switches;
    for (const auto& attachment : config.network_infrastructure.attachments) {
      if (attachment.network != network.id || attachment.address.empty() ||
          (attachment.kind != AttachmentKind::container_veth &&
           attachment.kind != AttachmentKind::namespace_veth))
        continue;
      std::string ofport;
      if (run({"ovs-vsctl", "get", "Interface", attachment.peer, "ofport"}, &ofport) != 0 ||
          ofport.empty() || ofport == "-1")
        throw std::runtime_error("cannot resolve OVS port for profile attachment " + attachment.id);
      const auto address = address_host(attachment.address);
      const auto add_flow = [&](const std::string& match) {
        if (run({"ovs-ofctl", "add-flow", attachment.network_switch,
                 "cookie=" + cookie + ",priority=200," + match + ",actions=output:" + ofport}) != 0)
          throw std::runtime_error("cannot install semantic-profile flow for " + attachment.id);
      };
      add_flow("ip,nw_dst=" + address);
      add_flow("arp,arp_tpa=" + address);
      switches.insert(attachment.network_switch);
    }
    for (const auto& network_switch : switches) {
      const auto router = std::find_if(config.network_infrastructure.attachments.begin(),
                                       config.network_infrastructure.attachments.end(),
                                       [&](const auto& attachment) {
                                         return attachment.network == network.id &&
                                                attachment.network_switch == network_switch &&
                                                attachment.kind == AttachmentKind::namespace_veth;
                                       });
      if (router != config.network_infrastructure.attachments.end()) {
        std::string ofport;
        if (run({"ovs-vsctl", "get", "Interface", router->peer, "ofport"}, &ofport) != 0 ||
            ofport.empty() || ofport == "-1" ||
            run({"ovs-ofctl", "add-flow", network_switch,
                 "cookie=" + cookie +
                     ",priority=150,ip,dl_dst=00:00:00:00:00:00/01:00:00:00:00:00,actions=output:" +
                     ofport}) != 0)
          throw std::runtime_error("cannot install IPvlan router fallback flow");
      }
      if (run({"ovs-ofctl", "add-flow", network_switch,
               "cookie=" + cookie + ",priority=100,dl_dst=ff:ff:ff:ff:ff:ff,actions=drop"}) != 0 ||
          run({"ovs-ofctl", "add-flow", network_switch,
               "cookie=" + cookie +
                   ",priority=90,dl_dst=01:00:00:00:00:00/01:00:00:00:00:00,actions=drop"}) != 0)
        throw std::runtime_error("cannot install IPvlan broadcast/multicast isolation");
    }
  }
}

}  // namespace graphx::infra::detail
