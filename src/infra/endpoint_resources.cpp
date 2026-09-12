#include "infra/endpoint_resources.hpp"

#include "infra/command_runner.hpp"
#include "infra/namespace_resources.hpp"
#include "infra/ovs_resources.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <climits>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
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

std::vector<std::string> lines(std::string value) {
  std::vector<std::string> result;
  std::size_t offset{};
  while (offset < value.size()) {
    const auto end = value.find('\n', offset);
    result.push_back(value.substr(offset, end == std::string::npos ? end : end - offset));
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  return result;
}
}  // namespace

ResolvedContainer resolve_container(const GraphConfig& config, std::string_view owner) {
  const auto service =
      std::find_if(config.deployment.services.begin(), config.deployment.services.end(),
                   [&](const auto& candidate) { return candidate.node_id == owner; });
  if (service == config.deployment.services.end())
    throw std::runtime_error("no deployment service for container attachment owner " +
                             std::string(owner));
  std::string found;
  const auto project_label = "label=com.docker.compose.project=" + config.deployment.project;
  const auto service_label = "label=com.docker.compose.service=" + std::string(owner);
  if (run({"docker", "ps", "--filter", project_label, "--filter", service_label, "--filter",
           "status=running", "--format", "{{.ID}}"},
          &found) != 0)
    throw std::runtime_error("cannot resolve Docker deployment identity for " + std::string(owner));
  const auto matches = lines(found);
  if (matches.size() != 1)
    throw std::runtime_error("expected exactly one running container for Compose service " +
                             std::string(owner));
  std::string inspected;
  const std::string format =
      "{{.Id}}\n{{.State.Running}}\n{{.State.Pid}}\n{{.Config.Image}}\n"
      "{{index .Config.Labels \"com.docker.compose.project\"}}\n"
      "{{index .Config.Labels \"com.docker.compose.service\"}}";
  if (run({"docker", "inspect", "--format", format, matches.front()}, &inspected) != 0)
    throw std::runtime_error("cannot inspect Docker deployment identity for " + std::string(owner));
  const auto fields = lines(inspected);
  if (fields.size() != 6 || fields[1] != "true" || fields[3] != service->image ||
      fields[4] != config.deployment.project || fields[5] != owner)
    throw std::runtime_error("Docker deployment identity does not match declared service " +
                             std::string(owner));
  if (fields[0].size() != 64 || !std::all_of(fields[0].begin(), fields[0].end(), [](char value) {
        return std::isxdigit(static_cast<unsigned char>(value));
      }))
    throw std::runtime_error("Docker returned an invalid full container identity");
  std::size_t consumed{};
  const auto parsed_pid = std::stoul(fields[2], &consumed);
  if (consumed != fields[2].size() || parsed_pid == 0 || parsed_pid > UINT32_MAX)
    throw std::runtime_error("Docker returned an invalid container PID");
  struct stat metadata{};
  const auto namespace_path = "/proc/" + fields[2] + "/ns/net";
  if (::stat(namespace_path.c_str(), &metadata) != 0)
    throw std::runtime_error("cannot inspect container network namespace for " +
                             std::string(owner));
  return {fields[0], static_cast<std::uint32_t>(parsed_pid),
          static_cast<std::uint64_t>(metadata.st_ino)};
}

std::optional<std::uint32_t> link_ifindex(const std::string& name) {
  std::ifstream input("/sys/class/net/" + name + "/ifindex");
  std::uint32_t value{};
  if (!(input >> value)) return std::nullopt;
  return value;
}

std::string link_alias(const std::string& name) {
  std::ifstream input("/sys/class/net/" + name + "/ifalias");
  std::string value;
  std::getline(input, value);
  return value;
}

std::string endpoint_alias(const OwnershipState& state, std::string_view attachment,
                           std::string_view side) {
  return "graphx:" + state.owner_token + ":" + std::string(attachment) + ":" + std::string(side);
}

const ExpectedEndpoint& expected_endpoint(const OwnershipState& state, std::string_view id) {
  const auto found = std::find_if(state.expected_endpoints.begin(), state.expected_endpoints.end(),
                                  [&](const auto& candidate) { return candidate.id == id; });
  if (found == state.expected_endpoints.end())
    throw std::runtime_error("missing expected endpoint record " + std::string(id));
  return *found;
}

bool ovs_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  return ovs_get("Port", endpoint.stable_id, "_uuid") == endpoint.stable_id &&
         ovs_get("Interface", endpoint.secondary_id, "_uuid") == endpoint.secondary_id &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_owner") == state.owner_token &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_attachment") ==
             endpoint.attachment_id &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_config_hash") ==
             state.config_hash &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_graph") == state.graph_id &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_owner") ==
             state.owner_token &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_attachment") ==
             endpoint.attachment_id &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_config_hash") ==
             state.config_hash &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_graph") == state.graph_id;
}

bool host_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  const auto observed = link_ifindex(endpoint.name);
  return observed && endpoint.ifindex && *observed == *endpoint.ifindex &&
         link_alias(endpoint.name) == endpoint_alias(state, endpoint.attachment_id, "host");
}

std::string tap_owner_identity(std::uint32_t uid, std::uint32_t gid) {
  return std::to_string(uid) + ":" + std::to_string(gid);
}

bool tap_owner_matches(const ExpectedEndpoint& expected) {
  std::string details;
  if (run({"ip", "tuntap", "show", "dev", expected.host_interface}, &details) != 0) return false;
  return details.find(" tap ") != std::string::npos &&
         details.find(" user " + std::to_string(expected.tap_uid)) != std::string::npos &&
         details.find(" group " + std::to_string(expected.tap_gid)) != std::string::npos;
}

bool endpoint_names_absent_or_recorded(const OwnedResourceIdentity& endpoint) {
  const auto port_uuid = ovs_get("Port", endpoint.name, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.name, "_uuid");
  return (port_uuid.empty() || port_uuid == endpoint.stable_id) &&
         (interface_uuid.empty() || interface_uuid == endpoint.secondary_id);
}

bool delete_owned_endpoint(const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  if (!endpoint_names_absent_or_recorded(endpoint)) return false;
  if (!endpoint.route_identity.empty()) {
    if (ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_owner") !=
            state.owner_token ||
        ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_attachment") !=
            endpoint.attachment_id)
      return false;
    if (run({"ovs-vsctl", "--", "remove", "Bridge",
             expected_endpoint(state, endpoint.attachment_id).network_switch, "mirrors",
             endpoint.route_identity, "--", "destroy", "Mirror", endpoint.route_identity}) != 0)
      return false;
  }
  if (ovs_get("Port", endpoint.stable_id, "_uuid") == endpoint.stable_id) {
    if (!ovs_endpoint_owned(endpoint, state)) return false;
    const auto& expected = expected_endpoint(state, endpoint.attachment_id);
    const auto bridge = std::find_if(
        state.bridges.begin(), state.bridges.end(),
        [&](const auto& candidate) { return candidate.name == expected.network_switch; });
    if (bridge == state.bridges.end()) return false;
    if (run({"ovs-vsctl",
             "--timeout=2",
             "--",
             "wait-until",
             "Port",
             endpoint.stable_id,
             "external_ids:graphx_owner=" + state.owner_token,
             "external_ids:graphx_attachment=" + endpoint.attachment_id,
             "external_ids:graphx_config_hash=" + state.config_hash,
             "external_ids:graphx_graph=" + state.graph_id,
             "--",
             "remove",
             "Bridge",
             bridge->stable_id,
             "ports",
             endpoint.stable_id,
             "--",
             "destroy",
             "Interface",
             endpoint.secondary_id,
             "--",
             "destroy",
             "Port",
             endpoint.stable_id}) != 0)
      return false;
  }
  if (link_ifindex(endpoint.name)) {
    if (!host_endpoint_owned(endpoint, state)) return false;
    const auto& expected = expected_endpoint(state, endpoint.attachment_id);
    if (expected.kind == AttachmentKind::qemu_tap) {
      if (!tap_owner_matches(expected) ||
          endpoint.tap_owner != tap_owner_identity(expected.tap_uid, expected.tap_gid) ||
          run({"ip", "tuntap", "delete", "dev", endpoint.name, "mode", "tap"}) != 0)
        return false;
    } else if (run({"ip", "link", "delete", "dev", endpoint.name}) != 0) {
      return false;
    }
  }
  return true;
}

bool tap_endpoint_healthy(const ExpectedEndpoint& expected, const OwnedResourceIdentity& endpoint,
                          const OwnershipState& state) {
  std::string link;
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state) &&
         endpoint_vlan_matches(expected, endpoint) && tap_owner_matches(expected) &&
         endpoint.tap_owner == tap_owner_identity(expected.tap_uid, expected.tap_gid) &&
         run({"ip", "-d", "-o", "link", "show", "dev", expected.host_interface}, &link) == 0 &&
         link.find("mtu " + std::to_string(expected.mtu)) != std::string::npos &&
         link.find("UP") != std::string::npos;
}

bool container_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint, const OwnershipState& state,
                                const GraphConfig& config) {
  const auto container = resolve_container(config, expected.owner);
  if (container.id != endpoint.container_id ||
      container.namespace_inode != *endpoint.namespace_inode)
    return false;
  std::string link;
  if (run({"nsenter", "-t", std::to_string(container.pid), "-n", "--", "ip", "-d", "-o", "link",
           "show", "dev", expected.target_interface},
          &link) != 0)
    return false;
  const auto colon = link.find(':');
  if (colon == std::string::npos || !endpoint.peer_ifindex ||
      std::stoul(link.substr(0, colon)) != *endpoint.peer_ifindex)
    return false;
  if (link.find("mtu " + std::to_string(expected.mtu)) == std::string::npos ||
      link.find(endpoint_alias(state, endpoint.attachment_id, "peer")) == std::string::npos ||
      (!expected.mac.empty() && link.find(expected.mac) == std::string::npos))
    return false;
  std::string address;
  if (run({"nsenter", "-t", std::to_string(container.pid), "-n", "--", "ip", "-o", "-4", "address",
           "show", "dev", expected.target_interface},
          &address) != 0 ||
      address.find(expected.address) == std::string::npos)
    return false;
  for (const auto& route : expected.routes) {
    std::string observed;
    if (run({"nsenter", "-t", std::to_string(container.pid), "-n", "--", "ip", "-4", "route",
             "show", route.destination},
            &observed) != 0 ||
        observed.find(route.destination) == std::string::npos ||
        observed.find("dev " + expected.target_interface) == std::string::npos ||
        (!route.via.empty() && observed.find("via " + route.via) == std::string::npos))
      return false;
  }
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state);
}

bool namespace_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint,
                                const OwnershipState& state) {
  const auto inode = namespace_inode(expected.namespace_name);
  if (!inode || !endpoint.namespace_inode || *inode != *endpoint.namespace_inode) return false;
  std::string link;
  if (run({"ip", "netns", "exec", expected.namespace_name, "ip", "-d", "-o", "link", "show", "dev",
           expected.target_interface},
          &link) != 0 ||
      link.find(endpoint_alias(state, endpoint.attachment_id, "peer")) == std::string::npos)
    return false;
  std::string address;
  if (run({"ip", "netns", "exec", expected.namespace_name, "ip", "-o", "-4", "address", "show",
           "dev", expected.target_interface},
          &address) != 0 ||
      address.find(expected.address) == std::string::npos)
    return false;
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state);
}

bool mirror_endpoint_healthy(const ExpectedEndpoint& expected,
                             const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state) &&
         link_ifindex(expected.target_interface) == endpoint.peer_ifindex &&
         link_alias(expected.target_interface) ==
             endpoint_alias(state, endpoint.attachment_id, "peer") &&
         !endpoint.route_identity.empty() &&
         ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_owner") ==
             state.owner_token &&
         ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_attachment") ==
             endpoint.attachment_id;
}

void check_endpoint_collision(const ExpectedEndpoint& endpoint, std::uint32_t pid) {
  if (link_ifindex(endpoint.host_interface))
    throw std::runtime_error("refusing unowned host interface collision: " +
                             endpoint.host_interface);
  if (!ovs_get("Port", endpoint.host_interface, "_uuid").empty() ||
      !ovs_get("Interface", endpoint.host_interface, "_uuid").empty())
    throw std::runtime_error("refusing unowned OVS endpoint collision: " + endpoint.host_interface);
  if (run({"nsenter", "-t", std::to_string(pid), "-n", "--", "ip", "link", "show", "dev",
           endpoint.target_interface}) == 0)
    throw std::runtime_error("refusing container interface collision: " +
                             endpoint.target_interface);
}

OwnedResourceIdentity create_endpoint(const ExpectedEndpoint& endpoint, std::uint32_t pid,
                                      const OwnershipState& state) {
  if (run({"ip", "link", "add", endpoint.host_interface, "type", "veth", "peer", "name",
           endpoint.target_interface}) != 0)
    throw std::runtime_error("cannot create veth for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "alias",
           endpoint_alias(state, endpoint.id, "peer")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.host_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0)
    throw std::runtime_error("cannot mark veth ownership for attachment " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  const auto peer_ifindex = link_ifindex(endpoint.target_interface);
  if (!ifindex || !peer_ifindex)
    throw std::runtime_error("cannot capture veth ifindices for attachment " + endpoint.id);
  const std::vector<std::string> ovs_command = {
      "ovs-vsctl",
      "--",
      "--id=@i",
      "create",
      "Interface",
      "name=" + endpoint.host_interface,
      "external_ids:graphx_owner=" + state.owner_token,
      "external_ids:graphx_attachment=" + endpoint.id,
      "external_ids:graphx_graph=" + state.graph_id,
      "external_ids:graphx_config_hash=" + state.config_hash,
      "--",
      "--id=@p",
      "create",
      "Port",
      "name=" + endpoint.host_interface,
      "interfaces=@i",
      "external_ids:graphx_owner=" + state.owner_token,
      "external_ids:graphx_attachment=" + endpoint.id,
      "external_ids:graphx_graph=" + state.graph_id,
      "external_ids:graphx_config_hash=" + state.config_hash,
      "--",
      "add",
      "Bridge",
      endpoint.network_switch,
      "ports",
      "@p"};
  if (run(ovs_command) != 0)
    throw std::runtime_error("cannot attach owned veth to OVS for attachment " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (port_uuid.empty() || interface_uuid.empty())
    throw std::runtime_error("cannot capture OVS endpoint identity for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.target_interface, "netns", std::to_string(pid)}) !=
      0)
    throw std::runtime_error("cannot move veth into container namespace for attachment " +
                             endpoint.id);
  const auto ns = [&](std::vector<std::string> command) {
    std::vector<std::string> prefix = {"nsenter", "-t", std::to_string(pid), "-n", "--"};
    prefix.insert(prefix.end(), command.begin(), command.end());
    if (run(prefix) != 0)
      throw std::runtime_error("cannot configure container endpoint " + endpoint.id);
  };
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "mtu", std::to_string(endpoint.mtu)});
  if (!endpoint.mac.empty())
    ns({"ip", "link", "set", "dev", endpoint.target_interface, "address", endpoint.mac});
  ns({"ip", "address", "replace", endpoint.address, "dev", endpoint.target_interface});
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "up"});
  for (const auto& route : endpoint.routes) {
    std::vector<std::string> command = {"ip", "route", "replace", route.destination};
    if (!route.via.empty()) command.insert(command.end(), {"via", route.via});
    command.insert(command.end(), {"dev", endpoint.target_interface});
    ns(std::move(command));
  }
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable host veth for attachment " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "container_veth";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.target_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *peer_ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  identity.container_id = endpoint.container_id;
  return identity;
}

OwnedResourceIdentity create_namespace_endpoint(const ExpectedEndpoint& endpoint,
                                                const OwnershipState& state) {
  if (run({"ip", "link", "add", endpoint.host_interface, "type", "veth", "peer", "name",
           endpoint.target_interface}) != 0)
    throw std::runtime_error("cannot create namespace veth for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "alias",
           endpoint_alias(state, endpoint.id, "peer")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.host_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0)
    throw std::runtime_error("cannot mark namespace veth " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  const auto peer_ifindex = link_ifindex(endpoint.target_interface);
  if (!ifindex || !peer_ifindex) throw std::runtime_error("cannot capture namespace veth identity");
  if (run({"ovs-vsctl",
           "--",
           "--id=@i",
           "create",
           "Interface",
           "name=" + endpoint.host_interface,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "--id=@p",
           "create",
           "Port",
           "name=" + endpoint.host_interface,
           "interfaces=@i",
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "add",
           "Bridge",
           endpoint.network_switch,
           "ports",
           "@p"}) != 0)
    throw std::runtime_error("cannot attach namespace veth to OVS " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (port_uuid.empty() || interface_uuid.empty())
    throw std::runtime_error("cannot capture namespace OVS identity");
  configure_endpoint_vlan(endpoint);
  if (run({"ip", "link", "set", "dev", endpoint.target_interface, "netns",
           endpoint.namespace_name}) != 0)
    throw std::runtime_error("cannot move veth into namespace " + endpoint.namespace_name);
  const auto ns = [&](std::vector<std::string> command) {
    std::vector<std::string> prefix = {"ip", "netns", "exec", endpoint.namespace_name};
    prefix.insert(prefix.end(), command.begin(), command.end());
    if (run(prefix) != 0)
      throw std::runtime_error("cannot configure namespace endpoint " + endpoint.id);
  };
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "mtu", std::to_string(endpoint.mtu)});
  if (!endpoint.mac.empty())
    ns({"ip", "link", "set", "dev", endpoint.target_interface, "address", endpoint.mac});
  ns({"ip", "address", "replace", endpoint.address, "dev", endpoint.target_interface});
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "up"});
  for (const auto& route : endpoint.routes) {
    std::vector<std::string> command = {"ip", "route", "replace", route.destination};
    if (!route.via.empty()) command.insert(command.end(), {"via", route.via});
    command.insert(command.end(), {"dev", endpoint.target_interface});
    ns(std::move(command));
  }
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable namespace host veth " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "namespace_veth";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.target_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *peer_ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  return identity;
}

OwnedResourceIdentity create_tap_endpoint(const ExpectedEndpoint& endpoint,
                                          const OwnershipState& state) {
  const auto uid = std::to_string(endpoint.tap_uid);
  const auto gid = std::to_string(endpoint.tap_gid);
  if (run({"ip", "tuntap", "add", "dev", endpoint.host_interface, "mode", "tap", "user", uid,
           "group", gid}) != 0)
    throw std::runtime_error("cannot create TAP for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.host_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0)
    throw std::runtime_error("cannot mark TAP ownership for attachment " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  if (!ifindex)
    throw std::runtime_error("cannot capture TAP ifindex for attachment " + endpoint.id);
  if (run({"ovs-vsctl",
           "--",
           "--id=@i",
           "create",
           "Interface",
           "name=" + endpoint.host_interface,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "--id=@p",
           "create",
           "Port",
           "name=" + endpoint.host_interface,
           "interfaces=@i",
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "add",
           "Bridge",
           endpoint.network_switch,
           "ports",
           "@p"}) != 0)
    throw std::runtime_error("cannot attach owned TAP to OVS for attachment " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (port_uuid.empty() || interface_uuid.empty())
    throw std::runtime_error("cannot capture TAP OVS identity for attachment " + endpoint.id);
  configure_endpoint_vlan(endpoint);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable TAP for attachment " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "qemu_tap";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.host_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  identity.tap_owner = tap_owner_identity(endpoint.tap_uid, endpoint.tap_gid);
  return identity;
}

std::string owned_mirror_name(const OwnershipState& state, std::string_view attachment) {
  return state.instance_id.empty()
             ? std::string(attachment)
             : instance_resource_name(state.graph_id, state.instance_id, "mirror", attachment);
}

OwnedResourceIdentity create_mirror_endpoint(const ExpectedEndpoint& endpoint,
                                             const OwnershipState& state) {
  if (run({"ip", "link", "add", endpoint.host_interface, "type", "veth", "peer", "name",
           endpoint.target_interface}) != 0)
    throw std::runtime_error("cannot create mirror veth " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "alias",
           endpoint_alias(state, endpoint.id, "peer")}) != 0)
    throw std::runtime_error("cannot mark mirror veth " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  const auto peer_ifindex = link_ifindex(endpoint.target_interface);
  if (!ifindex || !peer_ifindex) throw std::runtime_error("cannot capture mirror veth identity");
  if (run({"ovs-vsctl",
           "--",
           "--id=@i",
           "create",
           "Interface",
           "name=" + endpoint.host_interface,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "--id=@p",
           "create",
           "Port",
           "name=" + endpoint.host_interface,
           "interfaces=@i",
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "add",
           "Bridge",
           endpoint.network_switch,
           "ports",
           "@p"}) != 0)
    throw std::runtime_error("cannot attach mirror veth " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (run({"ovs-vsctl", "--", "--id=@m", "create", "Mirror",
           "name=" + owned_mirror_name(state, endpoint.id), "select_all=true",
           "output-port=" + port_uuid, "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash, "--", "add", "Bridge",
           endpoint.network_switch, "mirrors", "@m"}) != 0)
    throw std::runtime_error("cannot create OVS mirror " + endpoint.id);
  const auto mirror_uuid = ovs_get("Mirror", owned_mirror_name(state, endpoint.id), "_uuid");
  if (mirror_uuid.empty()) throw std::runtime_error("cannot capture OVS mirror identity");
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable mirror veth " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "mirror_veth";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.target_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.route_identity = mirror_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *peer_ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  return identity;
}

}  // namespace graphx::infra::detail
