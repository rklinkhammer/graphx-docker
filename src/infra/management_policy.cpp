#include "infra/management_policy.hpp"
#include "infra/endpoint_resources.hpp"
#include "infra/command_runner.hpp"
#include "config_document.hpp"
#include <algorithm>
#include <set>
#include <regex>

namespace graphx::infra::detail {
namespace {
using namespace config_internal;
std::string call(std::vector<std::string> args, std::string input = {}) {
  CommandOptions options;
  options.arguments = std::move(args);
  options.standard_input = std::move(input);
  options.capture_output = true;
  options.timeout_ms = 10000;
  options.output_limit = 1024 * 1024;
  const auto result = run_command(options);
  if (result.status || result.exec_error || result.output_truncated)
    throw std::runtime_error("E_MANAGEMENT_ACL: command failed");
  return result.output;
}
Value inspect(const std::string& id) {
  return parse_document(call({"docker", "inspect", id})).array().at(0);
}
std::vector<std::string> ns(const ResolvedContainer& container, std::vector<std::string> args) {
  std::vector<std::string> prefix{"nsenter", "-t", std::to_string(container.pid), "-n", "--"};
  prefix.insert(prefix.end(), args.begin(), args.end());
  return prefix;
}
void remove_handles(Value& value) {
  if (value.is_object()) {
    value.object().erase("handle");
    if (value.contains("counter")) value["counter"] = Object{};
    for (auto& [key, child] : value.object()) {
      (void)key;
      remove_handles(child);
    }
  } else if (value.is_array()) {
    for (auto& child : value.array()) remove_handles(child);
  }
}
std::string policy_hash(const ResolvedContainer& container) {
  return nft_policy_identity(
      ns(container, {"nft", "-j", "list", "table", "inet", "graphx_management"}));
}
std::string ipv4(const Value& value) {
  const auto& address = value.text();
  if (!std::regex_match(address, std::regex(R"(^([0-9]{1,3}\.){3}[0-9]{1,3}$)")))
    throw std::runtime_error("E_MANAGEMENT_ACL: invalid Docker IPv4 address");
  return address;
}
}  // namespace

std::string nft_policy_identity(const std::vector<std::string>& command) {
  auto value = parse_document(call(command));
  auto& rules = value["nftables"].array();
  std::erase_if(rules, [](const auto& item) { return item.contains("metainfo"); });
  remove_handles(value);
  return sha256(config_value_json(value));
}

void install_management_policy(const GraphConfig& config, OwnershipState& state) {
  std::set<std::string> owners;
  for (const auto& endpoint : state.expected_endpoints)
    if (endpoint.kind == AttachmentKind::container_veth) owners.insert(endpoint.owner);
  if (owners.empty()) return;
  const auto platform = resolve_owned_container(config, "platform", state);
  const auto platform_object = inspect(platform.id);
  call(ns(platform, {"sysctl", "-q", "-w", "net.ipv4.ip_forward=0"}));
  for (const auto& owner : owners) {
    const auto container = resolve_owned_container(config, owner, state);
    const auto object = inspect(container.id);
    const auto network_name = "graphx-" + state.graph_id + "-mg-" + owner;
    const auto& membership = object.at("NetworkSettings").at("Networks").at(network_name);
    const auto& platform_membership =
        platform_object.at("NetworkSettings").at("Networks").at(network_name);
    const auto record = std::ranges::find_if(state.processes, [&](const auto& item) {
      return item.kind == "network" && item.name == network_name;
    });
    if (record == state.processes.end() || membership.at("NetworkID") != Value(record->stable_id) ||
        platform_membership.at("NetworkID") != Value(record->stable_id))
      throw std::runtime_error("E_MANAGEMENT_ACL: management network identity mismatch");
    const auto node_ip = ipv4(membership.at("IPAddress"));
    const auto platform_ip = ipv4(platform_membership.at("IPAddress"));
    const auto links = parse_document(call(ns(container, {"ip", "-j", "-4", "address", "show"})));
    std::string interface;
    for (const auto& link : links.array())
      for (const auto& address : link.at("addr_info").array())
        if (address.at("local") == Value(node_ip)) {
          if (!interface.empty()) throw std::runtime_error("E_MANAGEMENT_ACL: ambiguous interface");
          interface = link.at("ifname").text();
        }
    if (!std::regex_match(interface, std::regex("^[a-zA-Z0-9_-]{1,15}$")))
      throw std::runtime_error("E_MANAGEMENT_ACL: management interface not found");
    const auto tcp =
        std::to_string(config.resolved.at("platform").at("console").at("port").integer());
    const auto udp =
        std::to_string(config.resolved.at("platform").at("telemetry").at("port").integer());
    // No counters: the normalized nft JSON remains a stable policy identity.
    const auto rules =
        "table inet graphx_management {\n comment \"graphx:" + state.owner_token +
        "\";\n chain input { type filter hook input priority -10; policy accept;\n iifname \"" +
        interface + "\" ip saddr " + platform_ip +
        " ct state established,related accept\n iifname \"" + interface +
        "\" drop\n }\n chain output { type filter hook output priority -10; policy accept;\n "
        "oifname \"" +
        interface + "\" ip daddr " + platform_ip + " tcp dport " + tcp + " accept\n oifname \"" +
        interface + "\" ip daddr " + platform_ip + " udp dport " + udp + " accept\n oifname \"" +
        interface +
        "\" drop\n }\n chain forward { type filter hook forward priority -10; policy drop; }\n}\n";
    call(ns(container, {"sysctl", "-q", "-w", "net.ipv4.ip_forward=0"}));
    // Refuse existing tables, including an interrupted earlier attempt, before nft can merge them.
    const auto tables = parse_document(call(ns(container, {"nft", "-j", "list", "tables"})));
    for (const auto& item : tables.at("nftables").array())
      if (item.contains("table") && item.at("table").at("family") == Value("inet") &&
          item.at("table").at("name") == Value("graphx_management"))
        throw std::runtime_error("E_MANAGEMENT_ACL: refusing table collision");
    call(ns(container, {"nft", "-f", "-"}), rules);
    const auto hash = policy_hash(container);
    for (auto& endpoint : state.expected_endpoints)
      if (endpoint.kind == AttachmentKind::container_veth && endpoint.owner == owner)
        endpoint.management_policy = hash;
  }
}

bool management_policy_matches(const GraphConfig& config, const OwnershipState& state,
                               bool allow_absent) {
  std::set<std::string> seen;
  for (const auto& endpoint : state.expected_endpoints) {
    if (endpoint.kind != AttachmentKind::container_veth || !seen.insert(endpoint.owner).second)
      continue;
    if (endpoint.management_policy.empty()) {
      if (allow_absent) continue;
      return false;
    }
    if (allow_absent && (!link_ifindex(endpoint.host_interface) ||
                         !inspect(endpoint.container_id).at("State").at("Running").boolean()))
      continue;
    const auto container = resolve_owned_container(config, endpoint.owner, state);
    if (container.namespace_inode != endpoint.namespace_inode ||
        policy_hash(container) != endpoint.management_policy)
      return false;
  }
  return true;
}
}  // namespace graphx::infra::detail
