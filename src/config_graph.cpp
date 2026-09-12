#include "config_internal.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace graphx::config_internal {

void ConfigParser::parse_nodes(const YAML::Node& nodes, GraphConfig& config) {
  if (!require_sequence(nodes, "graph.nodes")) return;
  if (nodes.size() == 0) error("graph.nodes", "must contain at least one node");
  if (nodes.size() > kMaxNodes) error("graph.nodes", "exceeds maximum node count 1024");
  std::unordered_set<std::string> ids;
  const auto count = std::min<std::size_t>(nodes.size(), kMaxNodes);
  for (std::size_t index = 0; index < count; ++index) {
    const auto path = "graph.nodes[" + std::to_string(index) + "]";
    const auto value = nodes[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path,
                {"id", "kind", "runtime", "execution", "lifecycle", "control", "accelerator",
                 "architecture", "ports", "sdr"});
    NodeConfig node;
    node.id = text(value["id"], path + ".id", 64);
    identifier(node.id, path + ".id");
    node.kind = text(value["kind"], path + ".kind", 64);
    identifier(node.kind, path + ".kind");
    if (value["runtime"]) node.runtime = text(value["runtime"], path + ".runtime", 32);
    if (node.runtime != "process" && node.runtime != "docker" && node.runtime != "qemu" &&
        node.runtime != "external")
      error(path + ".runtime", "must be 'process', 'docker', 'qemu', or 'external'");
    if (value["execution"]) node.execution = text(value["execution"], path + ".execution", 32);
    if (node.execution != "local" && node.execution != "host" && node.execution != "container")
      error(path + ".execution", "must be 'local', 'host', or 'container'");
    if (value["lifecycle"]) node.lifecycle = text(value["lifecycle"], path + ".lifecycle", 32);
    if (node.lifecycle != "managed" && node.lifecycle != "external")
      error(path + ".lifecycle", "must be 'managed' or 'external'");
    if (value["control"]) node.control = text(value["control"], path + ".control", 32);
    if (node.control != "graphx" && node.control != "origin" && node.control != "none")
      error(path + ".control", "must be 'graphx', 'origin', or 'none'");
    if (value["accelerator"])
      node.accelerator = text(value["accelerator"], path + ".accelerator", 16);
    if (!node.accelerator.empty() && node.accelerator != "auto" && node.accelerator != "kvm" &&
        node.accelerator != "tcg" && node.accelerator != "hvf")
      error(path + ".accelerator", "must be 'auto', 'kvm', 'tcg', or 'hvf'");
    if (!node.accelerator.empty() && node.runtime != "qemu")
      error(path + ".accelerator", "is supported only for runtime 'qemu'");
    if (value["architecture"])
      node.architecture = text(value["architecture"], path + ".architecture", 32);
    if (!node.architecture.empty() && node.architecture != "x86_64")
      error(path + ".architecture", "must be 'x86_64'");
    if (!node.architecture.empty() && node.runtime != "qemu")
      error(path + ".architecture", "is supported only for runtime 'qemu'");
    if (!node.id.empty() && !ids.insert(node.id).second)
      error(path + ".id", "duplicate node id '" + node.id + "'");
    parse_ports(value["ports"], path + ".ports", node);
    if (value["sdr"]) node.sdr = parse_sdr(value["sdr"], path + ".sdr");
    config.nodes.push_back(std::move(node));
  }
}

SdrConfig ConfigParser::parse_sdr(const YAML::Node& value, const std::string& path) {
  SdrConfig result;
  if (!require_map(value, path)) return result;
  strict_keys(
      value, path,
      {"samples_edge", "control_edge", "frequency_hz", "sample_interval_ms", "credentials"});
  result.samples_edge = strict_text(value["samples_edge"], path + ".samples_edge", 64);
  result.control_edge = strict_text(value["control_edge"], path + ".control_edge", 64);
  identifier(result.samples_edge, path + ".samples_edge");
  identifier(result.control_edge, path + ".control_edge");
  if (value["frequency_hz"])
    result.frequency_hz = strict_unsigned_64_value(value["frequency_hz"], path + ".frequency_hz");
  if (result.frequency_hz < 1000000 || result.frequency_hz > 6000000000ULL)
    error(path + ".frequency_hz", "must be between 1000000 and 6000000000");
  if (value["sample_interval_ms"])
    result.sample_interval_ms =
        strict_unsigned_value(value["sample_interval_ms"], path + ".sample_interval_ms");
  if (result.sample_interval_ms < 20 || result.sample_interval_ms > 60000)
    error(path + ".sample_interval_ms", "must be between 20 and 60000");
  const auto credentials = value["credentials"];
  if (require_map(credentials, path + ".credentials")) {
    strict_keys(credentials, path + ".credentials",
                {"ca_file", "certificate_file", "private_key_file", "server_name"});
    auto file = [&](std::string_view name) {
      const auto field = path + ".credentials." + std::string(name);
      const auto result_path = strict_text(credentials[std::string(name)], field, 1024);
      if (result_path.empty() || result_path.front() != '/' ||
          std::ranges::any_of(result_path, [](unsigned char c) { return c < 32 || c == 127; }))
        error(field, "must be an absolute path without control characters");
      return result_path;
    };
    result.credentials.ca_file = file("ca_file");
    result.credentials.certificate_file = file("certificate_file");
    result.credentials.private_key_file = file("private_key_file");
    result.credentials.server_name =
        strict_text(credentials["server_name"], path + ".credentials.server_name", 253);
    if (!std::regex_match(result.credentials.server_name,
                          std::regex("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$")))
      error(path + ".credentials.server_name", "must be a bounded TLS server name");
  }
  return result;
}

void ConfigParser::validate_sdr(const GraphConfig& config) {
  for (const auto& node : config.nodes) {
    if (!node.sdr) continue;
    const auto path = "graph.nodes." + node.id + ".sdr";
    if (config.deployment.instance_id.empty()) error(path, "requires deployment.instance_id");
    const auto& sdr = *node.sdr;
    const auto samples = std::ranges::find_if(
        config.edges, [&](const auto& edge) { return edge.edge.id == sdr.samples_edge; });
    const auto control = std::ranges::find_if(
        config.edges, [&](const auto& edge) { return edge.edge.id == sdr.control_edge; });
    if (samples == config.edges.end() || samples->data_plane != "external" ||
        transport_kind(samples->transport) != TransportKind::udp ||
        samples->edge.from_node != node.id)
      error(path + ".samples_edge", "must reference an external UDP edge originating at this node");
    if (control == config.edges.end() || control->data_plane != "external" ||
        transport_kind(control->transport) != TransportKind::tcp ||
        control->edge.to_node != node.id)
      error(path + ".control_edge", "must reference an external TCP edge terminating at this node");
    if (samples != config.edges.end() && transport_framing(samples->transport) != "none")
      error(path + ".samples_edge", "requires raw framing 'none'");
    if (control != config.edges.end() && control->data_plane == "external" &&
        transport_kind(control->transport) == TransportKind::tcp) {
      const auto& tcp = std::get<TcpTransportConfig>(
          std::get<ExternalTransportConfig>(control->transport).protocol);
      if (tcp.framing != "none" || !tcp.tls.enabled || !tcp.tls.verify_peer ||
          !tcp.tls.require_client_certificate || tcp.tls.server_name != sdr.credentials.server_name)
        error(path + ".control_edge", "requires raw mutual TLS with matching server_name");
    }
    if (samples != config.edges.end() && control != config.edges.end() &&
        samples->edge.to_node != control->edge.from_node)
      error(path, "sample receiver and controller must be the same node");
  }
}

void ConfigParser::parse_ports(const YAML::Node& ports, const std::string& path, NodeConfig& node) {
  if (!require_sequence(ports, path)) return;
  if (ports.size() > kMaxPortsPerNode) error(path, "exceeds maximum port count 256");
  std::unordered_set<std::string> names;
  const auto count = std::min<std::size_t>(ports.size(), kMaxPortsPerNode);
  for (std::size_t index = 0; index < count; ++index) {
    const auto item_path = path + "[" + std::to_string(index) + "]";
    const auto value = ports[index];
    if (!require_map(value, item_path)) continue;
    strict_keys(value, item_path, {"name", "direction", "schema"});
    Port port;
    port.name = text(value["name"], item_path + ".name", 64);
    identifier(port.name, item_path + ".name");
    const auto direction = text(value["direction"], item_path + ".direction", 16);
    if (direction == "input")
      port.direction = Direction::input;
    else if (direction == "output")
      port.direction = Direction::output;
    else
      error(item_path + ".direction", "must be 'input' or 'output'");
    port.schema = text(value["schema"], item_path + ".schema", 128);
    if (!port.name.empty() && !names.insert(port.name).second)
      error(item_path + ".name", "duplicate port name '" + port.name + "'");
    node.ports.push_back(std::move(port));
  }
}

std::pair<std::string, std::string> ConfigParser::endpoint(const std::string& value) {
  const auto dot = value.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 == value.size() ||
      value.find('.', dot + 1) != std::string::npos)
    return {};
  return {value.substr(0, dot), value.substr(dot + 1)};
}

void ConfigParser::parse_edges(const YAML::Node& edges, GraphConfig& config) {
  if (!require_sequence(edges, "graph.edges")) return;
  if (edges.size() > kMaxEdges) error("graph.edges", "exceeds maximum edge count 4096");
  std::unordered_set<std::string> ids;
  const auto count = std::min<std::size_t>(edges.size(), kMaxEdges);
  for (std::size_t index = 0; index < count; ++index) {
    const auto path = "graph.edges[" + std::to_string(index) + "]";
    const auto value = edges[index];
    if (!require_map(value, path)) continue;
    strict_keys(value, path, {"id", "from", "to", "transport", "data_plane"});
    EdgeConfig edge;
    edge.edge.id = text(value["id"], path + ".id", 64);
    identifier(edge.edge.id, path + ".id");
    if (!edge.edge.id.empty() && !ids.insert(edge.edge.id).second)
      error(path + ".id", "duplicate edge id '" + edge.edge.id + "'");
    const auto from_text = text(value["from"], path + ".from", 129);
    const auto to_text = text(value["to"], path + ".to", 129);
    std::tie(edge.edge.from_node, edge.edge.from_port) = endpoint(from_text);
    std::tie(edge.edge.to_node, edge.edge.to_port) = endpoint(to_text);
    if (edge.edge.from_node.empty()) error(path + ".from", "must be 'node.port'");
    if (edge.edge.to_node.empty()) error(path + ".to", "must be 'node.port'");
    const auto transport = text(value["transport"], path + ".transport", 32);
    edge.edge.transport = transport;
    if (value["data_plane"]) edge.data_plane = text(value["data_plane"], path + ".data_plane", 16);
    if (edge.data_plane != "graphx" && edge.data_plane != "external")
      error(path + ".data_plane", "must be 'graphx' or 'external'");
    if (transport == "tcp")
      edge.transport = edge.data_plane == "external"
                           ? TransportSettings{ExternalTransportConfig{TcpTransportConfig{}}}
                           : TransportSettings{TcpTransportConfig{}};
    else if (transport == "udp")
      edge.transport = edge.data_plane == "external"
                           ? TransportSettings{ExternalTransportConfig{UdpTransportConfig{}}}
                           : TransportSettings{UdpTransportConfig{}};
    else if (transport == "unix" && edge.data_plane == "graphx")
      edge.transport = UnixSocketTransportConfig{};
    else if (transport == "in_process" && edge.data_plane == "graphx")
      edge.transport = InProcessTransportConfig{};
    else if (transport == "shared_memory" && edge.data_plane == "graphx")
      edge.transport = SharedMemoryTransportConfig{};
    else if (transport == "unix" || transport == "in_process" || transport == "shared_memory")
      error(path + ".transport", "external data-plane edges support only TCP or UDP");
    else
      error(path + ".transport", "unsupported transport '" + transport + "'");
    config.edges.push_back(std::move(edge));
  }
}

}  // namespace graphx::config_internal
