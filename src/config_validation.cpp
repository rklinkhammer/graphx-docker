#include "config_internal.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace graphx::config_internal {

const Port* ConfigParser::find_port(const GraphConfig& config, const std::string& node_id,
                                    const std::string& port_name, const std::string& path) {
  const auto node =
      std::ranges::find_if(config.nodes, [&](const auto& value) { return value.id == node_id; });
  if (node == config.nodes.end()) {
    error(path, "references unknown node '" + node_id + "'");
    return nullptr;
  }
  const auto port =
      std::ranges::find_if(node->ports, [&](const auto& value) { return value.name == port_name; });
  if (port == node->ports.end()) {
    error(path, "references unknown port '" + node_id + "." + port_name + "'");
    return nullptr;
  }
  return &*port;
}

void ConfigParser::validate_graph(const GraphConfig& config) {
  std::unordered_map<std::string, std::vector<std::string>> adjacency;
  for (std::size_t index = 0; index < config.edges.size(); ++index) {
    const auto& edge = config.edges[index].edge;
    const auto path = "graph.edges[" + std::to_string(index) + "]";
    const auto* source = find_port(config, edge.from_node, edge.from_port, path + ".from");
    const auto* target = find_port(config, edge.to_node, edge.to_port, path + ".to");
    if (source && source->direction != Direction::output)
      error(path + ".from", "source port must have direction 'output'");
    if (target && target->direction != Direction::input)
      error(path + ".to", "target port must have direction 'input'");
    if (source && target && source->schema != target->schema)
      error(path, "schema mismatch: source is '" + source->schema + "' but target is '" +
                      target->schema + "'");
    // External data-plane edges describe traffic which GraphX observes but does
    // not schedule.  In particular, a device control connection may legitimately
    // run opposite to its sample stream.  Keep the managed GraphX execution DAG
    // acyclic while allowing that physical/network relationship to be modeled.
    if (config.edges[index].data_plane == "graphx" && !edge.from_node.empty() &&
        !edge.to_node.empty())
      adjacency[edge.from_node].push_back(edge.to_node);
  }

  enum class Visit { unseen, active, done };
  std::unordered_map<std::string, Visit> visits;
  std::function<bool(const std::string&)> visit = [&](const std::string& node) {
    if (visits[node] == Visit::active) return true;
    if (visits[node] == Visit::done) return false;
    visits[node] = Visit::active;
    for (const auto& target : adjacency[node])
      if (visit(target)) return true;
    visits[node] = Visit::done;
    return false;
  };
  for (const auto& node : config.nodes) {
    if (visit(node.id)) {
      error("graph.edges",
            "cycles are not supported in the GraphX-managed data plane by "
            "configuration version " +
                std::to_string(config.version));
      break;
    }
  }

  if (!config.deployment.services.empty()) {
    std::unordered_set<std::string> placed;
    for (const auto& service : config.deployment.services) {
      placed.insert(service.node_id);
      if (std::ranges::none_of(config.nodes,
                               [&](const auto& node) { return node.id == service.node_id; }))
        error("deployment.services." + service.node_id, "references an unknown graph node");
    }
    for (const auto& node : config.nodes)
      if (node.lifecycle == "external" && placed.contains(node.id))
        error("deployment.services." + node.id,
              "cannot manage a node whose lifecycle is 'external'");
      else if (node.lifecycle != "external" && !placed.contains(node.id))
        error("deployment.services", "missing placement for node '" + node.id + "'");
  }
  validate_network_infrastructure(config);
}

void ConfigParser::validate_network_infrastructure(const GraphConfig& config) {
  const auto& infrastructure = config.network_infrastructure;
  std::unordered_set<std::string> network_ids;
  std::unordered_map<std::string, std::vector<Ipv4Cidr>> subnets;
  for (std::size_t index = 0; index < infrastructure.networks.size(); ++index) {
    const auto& network = infrastructure.networks[index];
    const auto path = "network.networks[" + std::to_string(index) + "]";
    network_ids.insert(network.id);
    auto& parsed_subnets = subnets[network.id];
    std::unordered_set<std::string> seen_subnets;
    for (std::size_t subnet_index = 0; subnet_index < network.subnets.size(); ++subnet_index) {
      const auto subnet_path = path + ".subnets[" + std::to_string(subnet_index) + "]";
      const auto& literal_cidr = network.subnets[subnet_index];
      const auto subnet = ipv4_cidr(literal_cidr);
      if (!seen_subnets.insert(literal_cidr).second)
        error(subnet_path, "duplicates another network subnet");
      if (!subnet) {
        error(subnet_path, "must be an IPv4 CIDR");
        continue;
      }
      parsed_subnets.push_back(*subnet);
      const auto literal = literal_cidr.substr(0, literal_cidr.find('/'));
      const auto address = ipv4_address(literal);
      if (address && *address != subnet->network)
        error(subnet_path, "must use the network address for its prefix");
    }
    if (!network.gateway.empty()) {
      const auto gateway = ipv4_address(network.gateway);
      if (!gateway)
        error(path + ".gateway", "must be an IPv4 address");
      else if (std::ranges::none_of(parsed_subnets, [&](const auto& subnet) {
                 return (*gateway & subnet.mask) == subnet.network;
               }))
        error(path + ".gateway", "must be inside a network subnet");
    }
  }

  std::unordered_set<std::string> switch_ids;
  for (std::size_t index = 0; index < infrastructure.switches.size(); ++index) {
    const auto& network_switch = infrastructure.switches[index];
    const auto path = "network.switches[" + std::to_string(index) + "]";
    switch_ids.insert(network_switch.id);
    if (network_switch.mirror && std::ranges::none_of(network_switch.ports, [&](const auto& port) {
          return port.id == network_switch.mirror->output_port;
        }))
      error(path + ".mirror.output_port", "must reference a port on the same switch");
  }

  std::unordered_set<std::string> router_ids;
  for (std::size_t router_index = 0; router_index < infrastructure.routers.size(); ++router_index) {
    const auto& router = infrastructure.routers[router_index];
    const auto path = "network.routers[" + std::to_string(router_index) + "]";
    router_ids.insert(router.id);
    for (std::size_t index = 0; index < router.interfaces.size(); ++index) {
      const auto& interface = router.interfaces[index];
      const auto interface_path = path + ".interfaces[" + std::to_string(index) + "]";
      if (!network_ids.contains(interface.network))
        error(interface_path + ".network",
              "references unknown network '" + interface.network + "'");
      if (!switch_ids.contains(interface.network_switch))
        error(interface_path + ".switch",
              "references unknown switch '" + interface.network_switch + "'");
      const auto address = ipv4_cidr(interface.address);
      if (!address)
        error(interface_path + ".address", "must be an IPv4 CIDR");
      else if (const auto subnet = subnets.find(interface.network);
               subnet != subnets.end() &&
               std::ranges::none_of(subnet->second, [&](const auto& candidate) {
                 return address->network == candidate.network && address->mask == candidate.mask;
               }))
        error(interface_path + ".address", "must be inside one of its network subnets");
    }
    for (std::size_t index = 0; index < router.routes.size(); ++index) {
      if (std::ranges::count_if(router.routes, [&](const auto& candidate) {
            return candidate.destination == router.routes[index].destination;
          }) > 1)
        error(path + ".routes[" + std::to_string(index) + "].destination",
              "must be unique within the router");
      if (!ipv4_cidr(router.routes[index].destination))
        error(path + ".routes[" + std::to_string(index) + "].destination", "must be an IPv4 CIDR");
      else if (!router.routes[index].via.empty() && !ipv4_address(router.routes[index].via))
        error(path + ".routes[" + std::to_string(index) + "].via", "must be an IPv4 address");
      if (!router.routes[index].device.empty() &&
          std::ranges::none_of(router.interfaces, [&](const auto& interface) {
            return interface.device == router.routes[index].device;
          }))
        error(path + ".routes[" + std::to_string(index) + "].device",
              "references an unknown router interface device");
    }
    for (std::size_t index = 0; index < router.policies.size(); ++index) {
      const auto& policy = router.policies[index];
      const auto policy_path = path + ".policies[" + std::to_string(index) + "]";
      if (std::ranges::count_if(router.policies, [&](const auto& candidate) {
            return candidate.id == policy.id;
          }) > 1)
        error(policy_path + ".id", "must be unique within the router");
      if (!policy.source.empty() && !ipv4_cidr(policy.source))
        error(policy_path + ".source", "must be an IPv4 CIDR");
      if (!policy.destination.empty() && !ipv4_cidr(policy.destination))
        error(policy_path + ".destination", "must be an IPv4 CIDR");
    }
  }

  std::unordered_set<std::string> node_ids;
  for (const auto& node : config.nodes) node_ids.insert(node.id);
  std::unordered_set<std::string> placed_nodes;
  for (const auto& service : config.deployment.services) placed_nodes.insert(service.node_id);
  std::unordered_set<std::string> container_peers;
  std::unordered_set<std::string> container_targets;
  for (std::size_t index = 0; index < infrastructure.attachments.size(); ++index) {
    const auto& attachment = infrastructure.attachments[index];
    const auto path = "network.attachments[" + std::to_string(index) + "]";
    if (attachment.kind != AttachmentKind::mirror && !network_ids.contains(attachment.network))
      error(path + ".network", "references unknown network '" + attachment.network + "'");
    if (!attachment.address.empty()) {
      const auto address = ipv4_cidr(attachment.address);
      if (!address)
        error(path + ".address", "must be an IPv4 CIDR");
      else if (const auto subnet = subnets.find(attachment.network);
               subnet != subnets.end() &&
               std::ranges::none_of(subnet->second, [&](const auto& candidate) {
                 return address->network == candidate.network && address->mask == candidate.mask;
               }))
        error(path + ".address", "must be inside one of its network subnets");
    }
    const auto node = std::ranges::find_if(
        config.nodes, [&](const auto& candidate) { return candidate.id == attachment.owner; });
    const auto router = std::ranges::find_if(infrastructure.routers, [&](const auto& candidate) {
      return candidate.id == attachment.owner;
    });
    const auto network_switch = std::ranges::find_if(
        infrastructure.switches,
        [&](const auto& candidate) { return candidate.id == attachment.network_switch; });
    if (!attachment.network_switch.empty() && network_switch == infrastructure.switches.end())
      error(path + ".switch", "references unknown switch '" + attachment.network_switch + "'");
    switch (attachment.kind) {
      case AttachmentKind::container_veth:
        if (node == config.nodes.end() || !placed_nodes.contains(attachment.owner) ||
            node->runtime == "qemu")
          error(path + ".owner", "container_veth owner must be a deployed non-QEMU node");
        if (config.deployment.project.empty())
          error("deployment.project", "is required when container_veth attachments exist");
        if (!container_peers.insert(attachment.peer).second)
          error(path + ".peer", "must be unique for container_veth host interfaces");
        if (!container_targets.insert(attachment.owner + "\n" + attachment.interface).second)
          error(path + ".interface", "must be unique within its container owner");
        for (std::size_t route_index = 0; route_index < attachment.routes.size(); ++route_index) {
          const auto& route = attachment.routes[route_index];
          const auto route_path = path + ".routes[" + std::to_string(route_index) + "]";
          if (!ipv4_cidr(route.destination))
            error(route_path + ".destination", "must be an IPv4 CIDR");
          if (!route.via.empty() && !ipv4_address(route.via))
            error(route_path + ".via", "must be an IPv4 address");
        }
        break;
      case AttachmentKind::namespace_veth:
        if (router == infrastructure.routers.end() || router->kind != RouterKind::linux_namespace)
          error(path + ".owner", "namespace_veth owner must be a Linux namespace router");
        else if (std::ranges::count_if(router->interfaces, [&](const auto& interface) {
                   return interface.network == attachment.network &&
                          interface.address == attachment.address &&
                          interface.device == attachment.interface &&
                          interface.peer == attachment.peer &&
                          interface.network_switch == attachment.network_switch;
                 }) != 1)
          error(path, "namespace_veth must exactly match one interface on its owner router");
        break;
      case AttachmentKind::qemu_tap:
        if (node == config.nodes.end() || node->runtime != "qemu")
          error(path + ".owner", "qemu_tap owner must be a QEMU node");
        break;
      case AttachmentKind::external:
        if (node == config.nodes.end())
          error(path + ".owner", "external owner must be a graph node");
        break;
      case AttachmentKind::mirror:
        if (network_switch == infrastructure.switches.end() ||
            attachment.owner != attachment.network_switch)
          error(path + ".owner", "mirror owner and switch must name the same OVS switch");
        else if (!network_switch->mirror || network_switch->mirror->id != attachment.id)
          error(path + ".id", "must match the mirror configured on its OVS switch");
        else {
          const auto output_port = std::ranges::find_if(
              network_switch->ports,
              [&](const auto& port) { return port.id == network_switch->mirror->output_port; });
          if (output_port == network_switch->ports.end() ||
              output_port->interface != attachment.interface)
            error(path + ".interface", "must match the configured mirror output-port interface");
        }
        break;
    }
  }
  std::unordered_set<std::string> capture_targets;
  for (std::size_t index = 0; index < infrastructure.captures.size(); ++index) {
    const auto& capture = infrastructure.captures[index];
    const auto path = "network.captures[" + std::to_string(index) + "]";
    const auto attachment = std::ranges::find_if(
        infrastructure.attachments,
        [&](const auto& candidate) { return candidate.id == capture.attachment; });
    if (attachment == infrastructure.attachments.end() ||
        attachment->kind != AttachmentKind::mirror)
      error(path + ".attachment", "must reference a mirror attachment");
    if (!capture_targets.insert(capture.attachment).second)
      error(path + ".attachment", "must be unique across network captures");
  }
  std::unordered_set<std::string> fault_targets;
  for (std::size_t index = 0; index < infrastructure.faults.size(); ++index) {
    const auto& fault = infrastructure.faults[index];
    const auto path = "network.faults[" + std::to_string(index) + "]";
    const auto attachment = std::ranges::find_if(
        infrastructure.attachments,
        [&](const auto& candidate) { return candidate.id == fault.attachment; });
    if (attachment == infrastructure.attachments.end() ||
        (attachment->kind != AttachmentKind::container_veth &&
         attachment->kind != AttachmentKind::namespace_veth &&
         attachment->kind != AttachmentKind::qemu_tap))
      error(path + ".attachment", "must reference a realized data attachment");
    if (!fault_targets.insert(fault.attachment).second)
      error(path + ".attachment", "must be unique across network faults");
  }

  std::unordered_set<std::string> edge_ids;
  for (const auto& edge : config.edges) edge_ids.insert(edge.edge.id);
  std::unordered_set<std::string> known_hops = node_ids;
  known_hops.insert(network_ids.begin(), network_ids.end());
  known_hops.insert(switch_ids.begin(), switch_ids.end());
  known_hops.insert(router_ids.begin(), router_ids.end());
  for (std::size_t index = 0; index < infrastructure.edge_paths.size(); ++index) {
    const auto& path = infrastructure.edge_paths[index];
    const auto location = "network.edge_paths." + path.edge_id;
    if (!edge_ids.contains(path.edge_id)) error(location, "references an unknown graph edge");
    for (std::size_t hop = 0; hop < path.hops.size(); ++hop)
      if (!known_hops.contains(path.hops[hop]))
        error(location + "[" + std::to_string(hop) + "]",
              "references unknown hop '" + path.hops[hop] + "'");
  }
}

}  // namespace graphx::config_internal
