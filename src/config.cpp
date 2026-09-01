#include "graphx/config.hpp"
#include "graphx/framing.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace graphx {
namespace {

constexpr std::size_t kMaxTextLength = 1024;
const std::regex kIdentifier{"^[A-Za-z][A-Za-z0-9_-]{0,63}$"};
const std::regex kMacAddress{"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"};

struct Ipv4Cidr {
  std::uint32_t network{};
  std::uint32_t mask{};
  unsigned prefix{};
};

std::optional<std::uint32_t> ipv4_address(std::string_view value) {
  in_addr address{};
  const std::string source(value);
  if (::inet_pton(AF_INET, source.c_str(), &address) != 1) return std::nullopt;
  return ntohl(address.s_addr);
}

std::optional<Ipv4Cidr> ipv4_cidr(std::string_view value) {
  const auto slash = value.find('/');
  if (slash == std::string_view::npos) return std::nullopt;
  const auto address = ipv4_address(value.substr(0, slash));
  if (!address) return std::nullopt;
  unsigned prefix{};
  const auto prefix_text = value.substr(slash + 1);
  const auto result =
      std::from_chars(prefix_text.data(), prefix_text.data() + prefix_text.size(), prefix);
  if (result.ec != std::errc{} || result.ptr != prefix_text.data() + prefix_text.size() ||
      prefix > 32)
    return std::nullopt;
  const auto mask = prefix == 0 ? 0U : 0xffffffffU << (32U - prefix);
  return Ipv4Cidr{*address & mask, mask, prefix};
}

std::string diagnostics_message(const std::vector<ConfigDiagnostic>& diagnostics) {
  std::ostringstream message;
  message << "configuration validation failed with " << diagnostics.size() << " error";
  if (diagnostics.size() != 1) message << 's';
  for (const auto& diagnostic : diagnostics)
    message << "\n- " << diagnostic.path << ": " << diagnostic.message;
  return message.str();
}

std::vector<std::string> split_path(std::string_view value) {
  std::vector<std::string> parts;
  std::size_t start{};
  while (start <= value.size()) {
    const auto dot = value.find('.', start);
    parts.emplace_back(
        value.substr(start, dot == std::string_view::npos ? value.size() - start : dot - start));
    if (dot == std::string_view::npos) break;
    start = dot + 1;
  }
  return parts;
}

void apply_override(YAML::Node& root, const ConfigOverride& override,
                    std::vector<ConfigDiagnostic>& errors) {
  const auto parts = split_path(override.path);
  if (parts.empty() || std::ranges::any_of(parts, [](const auto& part) { return part.empty(); })) {
    errors.push_back({override.path, "override path must contain non-empty dotted components"});
    return;
  }
  YAML::Node current = root;
  for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
    if (!current.IsMap() || !current[parts[index]]) {
      errors.push_back({override.path, "override path does not exist"});
      return;
    }
    YAML::Node next = current[parts[index]];
    current.reset(next);
  }
  if (!current.IsMap() || !current[parts.back()]) {
    errors.push_back({override.path, "override path does not exist"});
    return;
  }
  current[parts.back()] = override.value;
}

class ConfigParser {
 public:
  explicit ConfigParser(YAML::Node root) : root_(std::move(root)) {}

  GraphConfig parse() {
    GraphConfig config;
    if (!require_map(root_, "$")) throw ConfigError(std::move(errors_));
    strict_keys(root_, "$",
                {"version", "graph", "transport", "network", "deployment", "observability"});
    config.version = unsigned_value(root_["version"], "version");
    if (config.version != kConfigVersion)
      error("version", "unsupported version " + std::to_string(config.version) +
                           "; this build supports version 1");

    const auto graph = root_["graph"];
    if (require_map(graph, "graph")) {
      strict_keys(graph, "graph", {"id", "nodes", "edges"});
      config.id = text(graph["id"], "graph.id", 64);
      identifier(config.id, "graph.id");
      parse_nodes(graph["nodes"], config);
      parse_edges(graph["edges"], config);
    }
    parse_transports(root_["transport"], config);
    parse_network_infrastructure(root_["network"], config);
    parse_deployment(root_["deployment"], config);
    parse_observability(root_["observability"], config);
    validate_graph(config);
    if (!errors_.empty()) throw ConfigError(std::move(errors_));
    return config;
  }

 private:
  void error(std::string path, std::string message) {
    errors_.push_back({std::move(path), std::move(message)});
  }

  bool require_map(const YAML::Node& node, const std::string& path) {
    if (!node) {
      error(path, "is required");
      return false;
    }
    if (!node.IsMap()) {
      error(path, "must be a mapping");
      return false;
    }
    return true;
  }

  bool require_sequence(const YAML::Node& node, const std::string& path) {
    if (!node) {
      error(path, "is required");
      return false;
    }
    if (!node.IsSequence()) {
      error(path, "must be a sequence");
      return false;
    }
    return true;
  }

  void strict_keys(const YAML::Node& node, const std::string& path,
                   std::initializer_list<std::string_view> allowed) {
    if (!node.IsMap()) return;
    std::unordered_set<std::string> seen;
    for (const auto& entry : node) {
      if (!entry.first.IsScalar()) {
        error(path, "contains a non-scalar key");
        continue;
      }
      const auto key = entry.first.Scalar();
      if (!seen.insert(key).second) error(path + "." + key, "duplicate key");
      if (std::ranges::find(allowed, key) == allowed.end())
        error(path + "." + key, "unknown property");
    }
  }

  std::string text(const YAML::Node& node, const std::string& path,
                   std::size_t maximum = kMaxTextLength) {
    if (!node) {
      error(path, "is required");
      return {};
    }
    if (!node.IsScalar()) {
      error(path, "must be a scalar string");
      return {};
    }
    const auto value = node.Scalar();
    if (value.empty()) error(path, "must not be empty");
    if (value.size() > maximum) error(path, "exceeds maximum length " + std::to_string(maximum));
    return value;
  }

  std::uint32_t unsigned_value(const YAML::Node& node, const std::string& path) {
    if (!node) {
      error(path, "is required");
      return 0;
    }
    try {
      return node.as<std::uint32_t>();
    } catch (const YAML::Exception&) {
      error(path, "must be an unsigned integer");
      return 0;
    }
  }

  bool bool_value(const YAML::Node& node, const std::string& path, bool fallback) {
    if (!node) return fallback;
    try {
      return node.as<bool>();
    } catch (const YAML::Exception&) {
      error(path, "must be a boolean");
      return fallback;
    }
  }

  void identifier(const std::string& value, const std::string& path) {
    if (!value.empty() && !std::regex_match(value, kIdentifier))
      error(path, "must match [A-Za-z][A-Za-z0-9_-]{0,63}");
  }

  void parse_nodes(const YAML::Node& nodes, GraphConfig& config) {
    if (!require_sequence(nodes, "graph.nodes")) return;
    if (nodes.size() == 0) error("graph.nodes", "must contain at least one node");
    if (nodes.size() > kMaxNodes) error("graph.nodes", "exceeds maximum node count 1024");
    std::unordered_set<std::string> ids;
    const auto count = std::min<std::size_t>(nodes.size(), kMaxNodes);
    for (std::size_t index = 0; index < count; ++index) {
      const auto path = "graph.nodes[" + std::to_string(index) + "]";
      const auto value = nodes[index];
      if (!require_map(value, path)) continue;
      strict_keys(value, path, {"id", "kind", "ports"});
      NodeConfig node;
      node.id = text(value["id"], path + ".id", 64);
      identifier(node.id, path + ".id");
      node.kind = text(value["kind"], path + ".kind", 64);
      identifier(node.kind, path + ".kind");
      if (!node.id.empty() && !ids.insert(node.id).second)
        error(path + ".id", "duplicate node id '" + node.id + "'");
      parse_ports(value["ports"], path + ".ports", node);
      config.nodes.push_back(std::move(node));
    }
  }

  void parse_ports(const YAML::Node& ports, const std::string& path, NodeConfig& node) {
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

  static std::pair<std::string, std::string> endpoint(const std::string& value) {
    const auto dot = value.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == value.size() ||
        value.find('.', dot + 1) != std::string::npos)
      return {};
    return {value.substr(0, dot), value.substr(dot + 1)};
  }

  void parse_edges(const YAML::Node& edges, GraphConfig& config) {
    if (!require_sequence(edges, "graph.edges")) return;
    if (edges.size() > kMaxEdges) error("graph.edges", "exceeds maximum edge count 4096");
    std::unordered_set<std::string> ids;
    const auto count = std::min<std::size_t>(edges.size(), kMaxEdges);
    for (std::size_t index = 0; index < count; ++index) {
      const auto path = "graph.edges[" + std::to_string(index) + "]";
      const auto value = edges[index];
      if (!require_map(value, path)) continue;
      strict_keys(value, path, {"id", "from", "to", "transport"});
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
      if (transport == "tcp")
        edge.transport.kind = TransportKind::tcp;
      else if (transport == "unix")
        edge.transport.kind = TransportKind::unix_socket;
      else if (transport == "in_process")
        edge.transport.kind = TransportKind::in_process;
      else if (transport == "shared_memory")
        edge.transport.kind = TransportKind::shared_memory;
      else
        error(path + ".transport", "unsupported transport '" + transport + "'");
      config.edges.push_back(std::move(edge));
    }
  }

  void parse_transports(const YAML::Node& transports, GraphConfig& config) {
    if (!require_map(transports, "transport")) return;
    strict_keys(transports, "transport", {"tcp", "unix", "in_process", "shared_memory"});
    const std::string_view sections[] = {"tcp", "unix", "in_process", "shared_memory"};
    std::unordered_set<std::string> consumed;
    for (auto& edge : config.edges) {
      const auto name = std::string(to_string(edge.transport.kind));
      const auto section = transports[name];
      const auto settings = section ? section[edge.edge.id] : YAML::Node{};
      const auto path = "transport." + name + "." + edge.edge.id;
      if (!require_map(settings, path)) continue;
      consumed.insert(name + "." + edge.edge.id);
      if (edge.transport.kind == TransportKind::tcp) {
        strict_keys(settings, path,
                    {"host", "bind", "port", "framing", "connect_timeout_ms", "send_timeout_ms", "reconnect",
                     "retry"});
        edge.transport.host = text(settings["host"], path + ".host", 253);
        edge.transport.bind = text(settings["bind"], path + ".bind", 253);
        const auto port = unsigned_value(settings["port"], path + ".port");
        if (port == 0 || port > 65535)
          error(path + ".port", "must be between 1 and 65535");
        else
          edge.transport.port = static_cast<std::uint16_t>(port);
        if (settings["framing"])
          edge.transport.framing = text(settings["framing"], path + ".framing", 16);
        if (settings["connect_timeout_ms"]) {
          edge.transport.connect_timeout_ms =
              unsigned_value(settings["connect_timeout_ms"], path + ".connect_timeout_ms");
          if (edge.transport.connect_timeout_ms == 0 || edge.transport.connect_timeout_ms > 600000)
            error(path + ".connect_timeout_ms", "must be between 1 and 600000");
        }
        if (settings["send_timeout_ms"]) {
          edge.transport.send_timeout_ms =
              unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
          if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
            error(path + ".send_timeout_ms", "must be between 1 and 600000");
        }
        edge.transport.reconnect = bool_value(settings["reconnect"], path + ".reconnect", true);
        if (const auto retry = settings["retry"]) {
          if (require_map(retry, path + ".retry")) {
            strict_keys(retry, path + ".retry",
                        {"max_attempts", "initial_backoff_ms", "max_backoff_ms"});
            if (retry["max_attempts"])
              edge.transport.retry_attempts =
                  unsigned_value(retry["max_attempts"], path + ".retry.max_attempts");
            if (retry["initial_backoff_ms"])
              edge.transport.retry_initial_backoff_ms = unsigned_value(
                  retry["initial_backoff_ms"], path + ".retry.initial_backoff_ms");
            if (retry["max_backoff_ms"])
              edge.transport.retry_max_backoff_ms =
                  unsigned_value(retry["max_backoff_ms"], path + ".retry.max_backoff_ms");
            if (edge.transport.retry_attempts == 0 || edge.transport.retry_attempts > 1000)
              error(path + ".retry.max_attempts", "must be between 1 and 1000");
            if (edge.transport.retry_initial_backoff_ms > 600000)
              error(path + ".retry.initial_backoff_ms", "must not exceed 600000");
            if (edge.transport.retry_max_backoff_ms > 600000)
              error(path + ".retry.max_backoff_ms", "must not exceed 600000");
            if (edge.transport.retry_max_backoff_ms < edge.transport.retry_initial_backoff_ms)
              error(path + ".retry.max_backoff_ms",
                    "must be greater than or equal to initial_backoff_ms");
          }
        }
      } else if (edge.transport.kind == TransportKind::unix_socket) {
        strict_keys(settings, path, {"path", "framing", "connect_timeout_ms", "send_timeout_ms"});
        edge.transport.path = text(settings["path"], path + ".path", 103);
        if (settings["framing"])
          edge.transport.framing = text(settings["framing"], path + ".framing", 16);
        if (settings["connect_timeout_ms"])
          edge.transport.connect_timeout_ms =
              unsigned_value(settings["connect_timeout_ms"], path + ".connect_timeout_ms");
        if (edge.transport.connect_timeout_ms == 0 || edge.transport.connect_timeout_ms > 600000)
          error(path + ".connect_timeout_ms", "must be between 1 and 600000");
        if (settings["send_timeout_ms"])
          edge.transport.send_timeout_ms =
              unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
        if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
          error(path + ".send_timeout_ms", "must be between 1 and 600000");
      } else if (edge.transport.kind == TransportKind::in_process) {
        strict_keys(settings, path, {"channel", "capacity", "backpressure", "send_timeout_ms"});
        edge.transport.channel = text(settings["channel"], path + ".channel", 64);
        identifier(edge.transport.channel, path + ".channel");
        if (settings["capacity"])
          edge.transport.capacity = unsigned_value(settings["capacity"], path + ".capacity");
        if (edge.transport.capacity == 0 || edge.transport.capacity > 65536)
          error(path + ".capacity", "must be between 1 and 65536");
        if (settings["backpressure"])
          edge.transport.backpressure = text(settings["backpressure"], path + ".backpressure", 16);
        if (edge.transport.backpressure != "block" && edge.transport.backpressure != "reject")
          error(path + ".backpressure", "must be 'block' or 'reject'");
        if (settings["send_timeout_ms"])
          edge.transport.send_timeout_ms =
              unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
        if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
          error(path + ".send_timeout_ms", "must be between 1 and 600000");
      } else {
        strict_keys(settings, path,
                    {"segment", "capacity", "max_message_bytes", "backpressure",
                     "connect_timeout_ms", "send_timeout_ms"});
        edge.transport.segment = text(settings["segment"], path + ".segment", 200);
        auto segment_id = edge.transport.segment;
        if (!segment_id.empty() && segment_id.front() == '/') segment_id.erase(0, 1);
        identifier(segment_id, path + ".segment");
        if (settings["capacity"])
          edge.transport.capacity = unsigned_value(settings["capacity"], path + ".capacity");
        if (edge.transport.capacity == 0 || edge.transport.capacity > 65536)
          error(path + ".capacity", "must be between 1 and 65536");
        if (settings["max_message_bytes"])
          edge.transport.max_message_bytes =
              unsigned_value(settings["max_message_bytes"], path + ".max_message_bytes");
        if (edge.transport.max_message_bytes < 64 ||
            edge.transport.max_message_bytes > kMaxFrameBytes + 4)
          error(path + ".max_message_bytes", "must be between 64 and 16777220");
        if (static_cast<std::uint64_t>(edge.transport.capacity) *
                    (edge.transport.max_message_bytes + 32ULL) +
                4096ULL >
            256ULL * 1024 * 1024)
          error(path, "shared-memory payload capacity must not exceed 256 MiB");
        if (settings["backpressure"])
          edge.transport.backpressure =
              text(settings["backpressure"], path + ".backpressure", 16);
        if (edge.transport.backpressure != "block" && edge.transport.backpressure != "reject")
          error(path + ".backpressure", "must be 'block' or 'reject'");
        if (settings["send_timeout_ms"])
          edge.transport.send_timeout_ms =
              unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
        if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
          error(path + ".send_timeout_ms", "must be between 1 and 600000");
        if (settings["connect_timeout_ms"])
          edge.transport.connect_timeout_ms =
              unsigned_value(settings["connect_timeout_ms"], path + ".connect_timeout_ms");
        if (edge.transport.connect_timeout_ms == 0 || edge.transport.connect_timeout_ms > 600000)
          error(path + ".connect_timeout_ms", "must be between 1 and 600000");
      }
      if (edge.transport.kind != TransportKind::in_process && edge.transport.framing != "u32be")
        error(path + ".framing", "version 1 supports only 'u32be'");
    }
    for (const auto section_name : sections) {
      const auto section = transports[std::string(section_name)];
      if (!section) continue;
      const auto path = "transport." + std::string(section_name);
      if (!require_map(section, path)) continue;
      std::unordered_set<std::string> seen;
      for (const auto& entry : section) {
        if (!entry.first.IsScalar()) {
          error(path, "contains a non-scalar edge id");
          continue;
        }
        const auto id = entry.first.Scalar();
        if (!seen.insert(id).second) error(path + "." + id, "duplicate transport entry");
        if (!consumed.contains(std::string(section_name) + "." + id))
          error(path + "." + id, "does not correspond to an edge using this transport");
      }
    }
  }

  VlanMetadata parse_vlan(const YAML::Node& value, const std::string& path) {
    VlanMetadata vlan;
    if (!value) return vlan;
    if (!require_map(value, path)) return vlan;
    strict_keys(value, path, {"access_tag", "trunks"});
    if (value["access_tag"]) {
      const auto tag = unsigned_value(value["access_tag"], path + ".access_tag");
      if (tag == 0 || tag > 4094)
        error(path + ".access_tag", "must be between 1 and 4094");
      else
        vlan.access_tag = static_cast<std::uint16_t>(tag);
    }
    const auto trunks = value["trunks"];
    if (trunks && require_sequence(trunks, path + ".trunks")) {
      for (std::size_t index = 0; index < trunks.size(); ++index) {
        const auto tag =
            unsigned_value(trunks[index], path + ".trunks[" + std::to_string(index) + "]");
        if (tag == 0 || tag > 4094)
          error(path + ".trunks[" + std::to_string(index) + "]", "must be between 1 and 4094");
        else
          vlan.trunks.push_back(static_cast<std::uint16_t>(tag));
      }
    }
    return vlan;
  }

  void parse_network_infrastructure(const YAML::Node& infrastructure, GraphConfig& config) {
    if (!infrastructure) return;
    if (!require_map(infrastructure, "network")) return;
    strict_keys(infrastructure, "network",
                {"networks", "switches", "routers", "interfaces", "edge_paths"});
    parse_networks(infrastructure["networks"], config);
    parse_switches(infrastructure["switches"], config);
    parse_routers(infrastructure["routers"], config);
    parse_network_interfaces(infrastructure["interfaces"], config);
    parse_edge_paths(infrastructure["edge_paths"], config);
  }

  void parse_networks(const YAML::Node& values, GraphConfig& config) {
    if (!values) return;
    if (!require_sequence(values, "network.networks")) return;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto path = "network.networks[" + std::to_string(index) + "]";
      const auto value = values[index];
      if (!require_map(value, path)) continue;
      strict_keys(value, path,
                  {"id", "driver", "subnet", "subnets", "gateway", "parent", "mode",
                   "external"});
      NetworkDefinition network;
      network.id = text(value["id"], path + ".id", 64);
      identifier(network.id, path + ".id");
      if (!network.id.empty() && !ids.insert(network.id).second)
        error(path + ".id", "duplicate network id '" + network.id + "'");
      const auto driver = text(value["driver"], path + ".driver", 16);
      if (driver == "bridge")
        network.driver = NetworkDriver::bridge;
      else if (driver == "macvlan")
        network.driver = NetworkDriver::macvlan;
      else if (driver == "ipvlan")
        network.driver = NetworkDriver::ipvlan;
      else
        error(path + ".driver", "must be 'bridge', 'macvlan', or 'ipvlan'");
      if (value["parent"]) network.parent = text(value["parent"], path + ".parent", 15);
      if (value["mode"]) network.mode = text(value["mode"], path + ".mode", 8);
      const bool layer_three = network.driver == NetworkDriver::ipvlan &&
                               (network.mode == "l3" || network.mode == "l3s");
      if (value["subnet"] && value["subnets"])
        error(path, "must use either 'subnet' or 'subnets', not both");
      if (value["subnet"])
        network.subnets.push_back(text(value["subnet"], path + ".subnet", 43));
      else if (value["subnets"] && require_sequence(value["subnets"], path + ".subnets")) {
        if (value["subnets"].size() == 0 || value["subnets"].size() > 16)
          error(path + ".subnets", "must contain between 1 and 16 subnets");
        for (std::size_t subnet_index = 0; subnet_index < value["subnets"].size(); ++subnet_index)
          network.subnets.push_back(text(value["subnets"][subnet_index],
                                         path + ".subnets[" + std::to_string(subnet_index) + "]",
                                         43));
      } else if (!value["subnet"])
        error(path + ".subnet", "or 'subnets' is required");
      if (!layer_three && network.subnets.size() > 1)
        error(path + ".subnets", "multiple subnets require ipvlan l3 or l3s mode");
      if (value["gateway"])
        network.gateway = text(value["gateway"], path + ".gateway", 39);
      else if (!layer_three)
        error(path + ".gateway", "is required except for ipvlan l3/l3s");
      network.external = bool_value(value["external"], path + ".external", true);
      if ((network.driver == NetworkDriver::macvlan || network.driver == NetworkDriver::ipvlan) &&
          network.parent.empty())
        error(path + ".parent", "is required for macvlan and ipvlan");
      if (network.driver == NetworkDriver::ipvlan && network.mode != "l2" && network.mode != "l3" &&
          network.mode != "l3s")
        error(path + ".mode", "must be 'l2', 'l3', or 'l3s' for ipvlan");
      config.network_infrastructure.networks.push_back(std::move(network));
    }
  }

  void parse_switches(const YAML::Node& values, GraphConfig& config) {
    if (!values) return;
    if (!require_sequence(values, "network.switches")) return;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto path = "network.switches[" + std::to_string(index) + "]";
      const auto value = values[index];
      if (!require_map(value, path)) continue;
      strict_keys(value, path, {"id", "kind", "datapath", "ports", "mirror"});
      SwitchDefinition network_switch;
      network_switch.id = text(value["id"], path + ".id", 15);
      identifier(network_switch.id, path + ".id");
      if (!network_switch.id.empty() && !ids.insert(network_switch.id).second)
        error(path + ".id", "duplicate switch id '" + network_switch.id + "'");
      const auto kind = text(value["kind"], path + ".kind", 32);
      if (kind != "openvswitch") error(path + ".kind", "version 1 supports only 'openvswitch'");
      if (value["datapath"])
        network_switch.datapath = text(value["datapath"], path + ".datapath", 16);
      if (network_switch.datapath != "system" && network_switch.datapath != "netdev")
        error(path + ".datapath", "must be 'system' or 'netdev'");
      const auto ports = value["ports"];
      if (ports && require_sequence(ports, path + ".ports")) {
        std::unordered_set<std::string> port_ids;
        for (std::size_t port_index = 0; port_index < ports.size(); ++port_index) {
          const auto port_path = path + ".ports[" + std::to_string(port_index) + "]";
          const auto port_value = ports[port_index];
          if (!require_map(port_value, port_path)) continue;
          strict_keys(port_value, port_path, {"id", "interface", "peer", "vlan"});
          SwitchPortDefinition port;
          port.id = text(port_value["id"], port_path + ".id", 64);
          identifier(port.id, port_path + ".id");
          if (!port.id.empty() && !port_ids.insert(port.id).second)
            error(port_path + ".id", "duplicate switch port id '" + port.id + "'");
          port.interface = text(port_value["interface"], port_path + ".interface", 15);
          if (port_value["peer"]) port.peer = text(port_value["peer"], port_path + ".peer", 15);
          port.vlan = parse_vlan(port_value["vlan"], port_path + ".vlan");
          network_switch.ports.push_back(std::move(port));
        }
      }
      const auto mirror = value["mirror"];
      if (mirror && require_map(mirror, path + ".mirror")) {
        strict_keys(mirror, path + ".mirror", {"id", "output_port", "select_all"});
        MirrorDefinition definition;
        definition.id = text(mirror["id"], path + ".mirror.id", 64);
        identifier(definition.id, path + ".mirror.id");
        definition.output_port = text(mirror["output_port"], path + ".mirror.output_port", 64);
        definition.select_all = bool_value(mirror["select_all"], path + ".mirror.select_all", true);
        network_switch.mirror = std::move(definition);
      }
      config.network_infrastructure.switches.push_back(std::move(network_switch));
    }
  }

  void parse_routers(const YAML::Node& values, GraphConfig& config) {
    if (!values) return;
    if (!require_sequence(values, "network.routers")) return;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto path = "network.routers[" + std::to_string(index) + "]";
      const auto value = values[index];
      if (!require_map(value, path)) continue;
      strict_keys(value, path,
                  {"id", "kind", "namespace", "forwarding", "interfaces", "routes", "policies"});
      RouterDefinition router;
      router.id = text(value["id"], path + ".id", 64);
      identifier(router.id, path + ".id");
      if (!router.id.empty() && !ids.insert(router.id).second)
        error(path + ".id", "duplicate router id '" + router.id + "'");
      const auto kind = text(value["kind"], path + ".kind", 32);
      if (kind == "linux_namespace")
        router.kind = RouterKind::linux_namespace;
      else if (kind == "container")
        router.kind = RouterKind::container;
      else
        error(path + ".kind", "must be 'linux_namespace' or 'container'");
      if (value["namespace"])
        router.namespace_name = text(value["namespace"], path + ".namespace", 64);
      if (router.kind == RouterKind::linux_namespace && router.namespace_name.empty())
        error(path + ".namespace", "is required for a Linux namespace router");
      router.forwarding = bool_value(value["forwarding"], path + ".forwarding", true);
      const auto interfaces = value["interfaces"];
      if (!require_sequence(interfaces, path + ".interfaces")) continue;
      for (std::size_t interface_index = 0; interface_index < interfaces.size();
           ++interface_index) {
        const auto interface_path = path + ".interfaces[" + std::to_string(interface_index) + "]";
        const auto interface_value = interfaces[interface_index];
        if (!require_map(interface_value, interface_path)) continue;
        strict_keys(interface_value, interface_path,
                    {"id", "network", "address", "device", "peer", "switch"});
        RouterInterfaceDefinition interface;
        interface.id = text(interface_value["id"], interface_path + ".id", 64);
        identifier(interface.id, interface_path + ".id");
        interface.network = text(interface_value["network"], interface_path + ".network", 64);
        interface.address = text(interface_value["address"], interface_path + ".address", 43);
        interface.device = text(interface_value["device"], interface_path + ".device", 15);
        interface.peer = text(interface_value["peer"], interface_path + ".peer", 15);
        interface.network_switch = text(interface_value["switch"], interface_path + ".switch", 15);
        router.interfaces.push_back(std::move(interface));
      }
      if (router.interfaces.size() < 2)
        error(path + ".interfaces", "must contain at least two interfaces");
      const auto routes = value["routes"];
      if (routes && require_sequence(routes, path + ".routes")) {
        for (std::size_t route_index = 0; route_index < routes.size(); ++route_index) {
          const auto route_path = path + ".routes[" + std::to_string(route_index) + "]";
          const auto route_value = routes[route_index];
          if (!require_map(route_value, route_path)) continue;
          strict_keys(route_value, route_path, {"destination", "via", "device"});
          RouteDefinition route;
          route.destination = text(route_value["destination"], route_path + ".destination", 43);
          if (route_value["via"]) route.via = text(route_value["via"], route_path + ".via", 39);
          if (route_value["device"])
            route.device = text(route_value["device"], route_path + ".device", 15);
          router.routes.push_back(std::move(route));
        }
      }
      const auto policies = value["policies"];
      if (policies && require_sequence(policies, path + ".policies")) {
        for (std::size_t policy_index = 0; policy_index < policies.size(); ++policy_index) {
          const auto policy_path = path + ".policies[" + std::to_string(policy_index) + "]";
          const auto policy_value = policies[policy_index];
          if (!require_map(policy_value, policy_path)) continue;
          strict_keys(policy_value, policy_path, {"id", "source", "destination", "action"});
          PolicyDefinition policy;
          policy.id = text(policy_value["id"], policy_path + ".id", 64);
          identifier(policy.id, policy_path + ".id");
          if (policy_value["source"])
            policy.source = text(policy_value["source"], policy_path + ".source", 43);
          if (policy_value["destination"])
            policy.destination =
                text(policy_value["destination"], policy_path + ".destination", 43);
          if (policy_value["action"])
            policy.action = text(policy_value["action"], policy_path + ".action", 16);
          if (policy.action != "accept" && policy.action != "drop")
            error(policy_path + ".action", "must be 'accept' or 'drop'");
          router.policies.push_back(std::move(policy));
        }
      }
      config.network_infrastructure.routers.push_back(std::move(router));
    }
  }

  void parse_network_interfaces(const YAML::Node& owners, GraphConfig& config) {
    if (!owners) return;
    if (!require_map(owners, "network.interfaces")) return;
    for (const auto& owner_entry : owners) {
      if (!owner_entry.first.IsScalar()) {
        error("network.interfaces", "contains a non-scalar owner");
        continue;
      }
      const auto owner = owner_entry.first.Scalar();
      const auto path = "network.interfaces." + owner;
      identifier(owner, path);
      if (!require_sequence(owner_entry.second, path)) continue;
      for (std::size_t index = 0; index < owner_entry.second.size(); ++index) {
        const auto item_path = path + "[" + std::to_string(index) + "]";
        const auto value = owner_entry.second[index];
        if (!require_map(value, item_path)) continue;
        strict_keys(value, item_path, {"id", "network", "address", "mac"});
        NetworkInterfaceDefinition interface;
        interface.owner = owner;
        interface.id = text(value["id"], item_path + ".id", 64);
        identifier(interface.id, item_path + ".id");
        interface.network = text(value["network"], item_path + ".network", 64);
        if (value["address"])
          interface.address = text(value["address"], item_path + ".address", 43);
        if (value["mac"]) interface.mac = text(value["mac"], item_path + ".mac", 17);
        if (!interface.mac.empty() && !std::regex_match(interface.mac, kMacAddress))
          error(item_path + ".mac", "must be a six-octet MAC address");
        config.network_infrastructure.interfaces.push_back(std::move(interface));
      }
    }
  }

  void parse_edge_paths(const YAML::Node& paths, GraphConfig& config) {
    if (!paths) return;
    if (!require_map(paths, "network.edge_paths")) return;
    for (const auto& entry : paths) {
      if (!entry.first.IsScalar()) {
        error("network.edge_paths", "contains a non-scalar edge id");
        continue;
      }
      EdgeNetworkPath path;
      path.edge_id = entry.first.Scalar();
      const auto item_path = "network.edge_paths." + path.edge_id;
      if (!require_sequence(entry.second, item_path)) continue;
      if (entry.second.size() < 2) error(item_path, "must contain at least two hops");
      for (std::size_t index = 0; index < entry.second.size(); ++index)
        path.hops.push_back(
            text(entry.second[index], item_path + "[" + std::to_string(index) + "]", 64));
      config.network_infrastructure.edge_paths.push_back(std::move(path));
    }
  }

  void parse_deployment(const YAML::Node& deployment, GraphConfig& config) {
    if (!deployment) return;
    if (!require_map(deployment, "deployment")) return;
    strict_keys(deployment, "deployment", {"network", "services", "telemetry"});
    if (deployment["network"])
      config.deployment.network = text(deployment["network"], "deployment.network", 128);

    const auto services = deployment["services"];
    if (services && require_map(services, "deployment.services")) {
      std::unordered_set<std::string> seen;
      for (const auto& entry : services) {
        if (!entry.first.IsScalar()) {
          error("deployment.services", "contains a non-scalar node id");
          continue;
        }
        const auto node_id = entry.first.Scalar();
        const auto path = "deployment.services." + node_id;
        identifier(node_id, path);
        if (!seen.insert(node_id).second) error(path, "duplicate service placement");
        if (!require_map(entry.second, path)) continue;
        strict_keys(entry.second, path, {"image", "command"});
        DeploymentService service;
        service.node_id = node_id;
        service.image = text(entry.second["image"], path + ".image");
        service.command = text(entry.second["command"], path + ".command");
        config.deployment.services.push_back(std::move(service));
      }
    }

    const auto telemetry = deployment["telemetry"];
    if (telemetry && require_map(telemetry, "deployment.telemetry")) {
      strict_keys(telemetry, "deployment.telemetry", {"service", "port"});
      config.deployment.telemetry_service =
          text(telemetry["service"], "deployment.telemetry.service", 128);
      const auto port = unsigned_value(telemetry["port"], "deployment.telemetry.port");
      if (port == 0 || port > 65535)
        error("deployment.telemetry.port", "must be between 1 and 65535");
      else
        config.deployment.telemetry_port = static_cast<std::uint16_t>(port);
    }
  }

  void parse_signal(const YAML::Node& value, const std::string& path,
                    ObservabilitySignalConfig& signal, bool allow_otlp) {
    if (!value) return;
    if (!require_map(value, path)) return;
    strict_keys(value, path, {"enabled", "exporters"});
    signal.enabled = bool_value(value["enabled"], path + ".enabled", true);
    if (!value["exporters"]) return;
    if (!require_sequence(value["exporters"], path + ".exporters")) return;
    signal.exporters.clear();
    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < value["exporters"].size(); ++index) {
      const auto item_path = path + ".exporters[" + std::to_string(index) + "]";
      auto exporter = text(value["exporters"][index], item_path, 32);
      if (exporter != "console" && exporter != "udp-json" &&
          (!allow_otlp || exporter != "otlp-http"))
        error(item_path, allow_otlp ? "must be 'console', 'udp-json', or 'otlp-http'"
                                    : "must be 'console' or 'udp-json'");
      if (!exporter.empty() && !seen.insert(exporter).second)
        error(item_path, "duplicate exporter '" + exporter + "'");
      signal.exporters.push_back(std::move(exporter));
    }
    if (signal.enabled && signal.exporters.empty())
      error(path + ".exporters", "must not be empty when enabled");
  }

  void parse_observability(const YAML::Node& value, GraphConfig& config) {
    if (!value) return;
    if (!require_map(value, "observability")) return;
    strict_keys(value, "observability", {"metrics", "tracing", "telemetry", "capture"});
    parse_signal(value["metrics"], "observability.metrics", config.observability.metrics, false);
    parse_signal(value["tracing"], "observability.tracing", config.observability.tracing, true);
    if (const auto telemetry = value["telemetry"]) {
      if (require_map(telemetry, "observability.telemetry")) {
        strict_keys(telemetry, "observability.telemetry",
                    {"host", "port", "websocket", "heartbeat_interval_ms",
                     "heartbeat_timeout_ms"});
        if (telemetry["host"])
          config.observability.telemetry.host =
              text(telemetry["host"], "observability.telemetry.host", 253);
        if (telemetry["port"]) {
          const auto port = unsigned_value(telemetry["port"], "observability.telemetry.port");
          if (port == 0 || port > 65535)
            error("observability.telemetry.port", "must be between 1 and 65535");
          else
            config.observability.telemetry.port = static_cast<std::uint16_t>(port);
        }
        if (telemetry["websocket"]) {
          config.observability.telemetry.websocket =
              text(telemetry["websocket"], "observability.telemetry.websocket", 128);
          if (!config.observability.telemetry.websocket.starts_with('/'))
            error("observability.telemetry.websocket", "must start with '/'");
        }
        if (telemetry["heartbeat_interval_ms"])
          config.observability.telemetry.heartbeat_interval_ms = unsigned_value(
              telemetry["heartbeat_interval_ms"],
              "observability.telemetry.heartbeat_interval_ms");
        if (telemetry["heartbeat_timeout_ms"])
          config.observability.telemetry.heartbeat_timeout_ms = unsigned_value(
              telemetry["heartbeat_timeout_ms"], "observability.telemetry.heartbeat_timeout_ms");
        if (config.observability.telemetry.heartbeat_interval_ms == 0 ||
            config.observability.telemetry.heartbeat_interval_ms > 600000)
          error("observability.telemetry.heartbeat_interval_ms",
                "must be between 1 and 600000");
        if (config.observability.telemetry.heartbeat_timeout_ms <
                config.observability.telemetry.heartbeat_interval_ms * 2ULL ||
            config.observability.telemetry.heartbeat_timeout_ms > 3600000)
          error("observability.telemetry.heartbeat_timeout_ms",
                "must be at least twice heartbeat_interval_ms and at most 3600000");
      }
    }
    if (const auto capture = value["capture"]) {
      if (require_map(capture, "observability.capture")) {
        strict_keys(capture, "observability.capture", {"enabled", "provider", "directory"});
        config.observability.capture.enabled =
            bool_value(capture["enabled"], "observability.capture.enabled", false);
        if (capture["provider"])
          config.observability.capture.provider =
              text(capture["provider"], "observability.capture.provider", 128);
        if (capture["directory"])
          config.observability.capture.directory =
              text(capture["directory"], "observability.capture.directory", 1024);
        if (config.observability.capture.enabled && config.observability.capture.provider.empty())
          error("observability.capture.provider", "is required when capture is enabled");
        if (config.observability.capture.enabled &&
            config.observability.capture.provider == "pcapng" &&
            config.observability.capture.directory.empty())
          error("observability.capture.directory", "is required for the pcapng provider");
      }
    }
  }

  const Port* find_port(const GraphConfig& config, const std::string& node_id,
                        const std::string& port_name, const std::string& path) {
    const auto node =
        std::ranges::find_if(config.nodes, [&](const auto& value) { return value.id == node_id; });
    if (node == config.nodes.end()) {
      error(path, "references unknown node '" + node_id + "'");
      return nullptr;
    }
    const auto port = std::ranges::find_if(
        node->ports, [&](const auto& value) { return value.name == port_name; });
    if (port == node->ports.end()) {
      error(path, "references unknown port '" + node_id + "." + port_name + "'");
      return nullptr;
    }
    return &*port;
  }

  void validate_graph(const GraphConfig& config) {
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
      if (!edge.from_node.empty() && !edge.to_node.empty())
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
        error("graph.edges", "cycles are not supported by configuration version 1");
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
        if (!placed.contains(node.id))
          error("deployment.services", "missing placement for node '" + node.id + "'");
    }
    validate_network_infrastructure(config);
  }

  void validate_network_infrastructure(const GraphConfig& config) {
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
        const auto subnet_path = network.subnets.size() == 1
                                     ? path + ".subnet"
                                     : path + ".subnets[" + std::to_string(subnet_index) + "]";
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
      if (network_switch.mirror &&
          std::ranges::none_of(network_switch.ports, [&](const auto& port) {
            return port.id == network_switch.mirror->output_port;
          }))
        error(path + ".mirror.output_port", "must reference a port on the same switch");
    }

    std::unordered_set<std::string> router_ids;
    for (std::size_t router_index = 0; router_index < infrastructure.routers.size();
         ++router_index) {
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
      for (std::size_t index = 0; index < router.routes.size(); ++index)
        if (!ipv4_cidr(router.routes[index].destination))
          error(path + ".routes[" + std::to_string(index) + "].destination",
                "must be an IPv4 CIDR");
        else if (!router.routes[index].via.empty() && !ipv4_address(router.routes[index].via))
          error(path + ".routes[" + std::to_string(index) + "].via", "must be an IPv4 address");
      for (std::size_t index = 0; index < router.policies.size(); ++index) {
        const auto& policy = router.policies[index];
        const auto policy_path = path + ".policies[" + std::to_string(index) + "]";
        if (!policy.source.empty() && !ipv4_cidr(policy.source))
          error(policy_path + ".source", "must be an IPv4 CIDR");
        if (!policy.destination.empty() && !ipv4_cidr(policy.destination))
          error(policy_path + ".destination", "must be an IPv4 CIDR");
      }
    }

    std::unordered_set<std::string> node_ids;
    for (const auto& node : config.nodes) node_ids.insert(node.id);
    for (std::size_t index = 0; index < infrastructure.interfaces.size(); ++index) {
      const auto& interface = infrastructure.interfaces[index];
      const auto path = "network.interfaces." + interface.owner + "[" + std::to_string(index) + "]";
      if (!node_ids.contains(interface.owner)) error(path, "owner is not a graph node");
      if (!network_ids.contains(interface.network))
        error(path + ".network", "references unknown network '" + interface.network + "'");
      if (!interface.address.empty()) {
        const auto address = ipv4_cidr(interface.address);
        if (!address)
          error(path + ".address", "must be an IPv4 CIDR");
        else if (const auto subnet = subnets.find(interface.network);
                 subnet != subnets.end() &&
                 std::ranges::none_of(subnet->second, [&](const auto& candidate) {
                   return address->network == candidate.network && address->mask == candidate.mask;
                 }))
          error(path + ".address", "must be inside one of its network subnets");
      }
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

  YAML::Node root_;
  std::vector<ConfigDiagnostic> errors_;
};

}  // namespace

ConfigError::ConfigError(std::vector<ConfigDiagnostic> diagnostics)
    : std::runtime_error(diagnostics_message(diagnostics)), diagnostics_(std::move(diagnostics)) {}

const NodeConfig& GraphConfig::node(std::string_view node_id) const {
  const auto found =
      std::ranges::find_if(nodes, [&](const auto& value) { return value.id == node_id; });
  if (found == nodes.end()) throw std::out_of_range("unknown node '" + std::string(node_id) + "'");
  return *found;
}

const EdgeConfig& GraphConfig::edge(std::string_view edge_id) const {
  const auto found =
      std::ranges::find_if(edges, [&](const auto& value) { return value.edge.id == edge_id; });
  if (found == edges.end()) throw std::out_of_range("unknown edge '" + std::string(edge_id) + "'");
  return *found;
}

std::vector<ConfigOverride> environment_overrides() {
  const auto* value = std::getenv("GRAPHX_OVERRIDES");
  if (!value || !*value) return {};
  std::vector<ConfigOverride> overrides;
  std::string_view remaining(value);
  while (!remaining.empty()) {
    const auto separator = remaining.find(';');
    const auto item = remaining.substr(0, separator);
    const auto equals = item.find('=');
    if (equals == std::string_view::npos || equals == 0)
      throw ConfigError(
          std::vector<ConfigDiagnostic>{{"GRAPHX_OVERRIDES", "each item must be path=value"}});
    overrides.push_back(
        {std::string(item.substr(0, equals)), std::string(item.substr(equals + 1))});
    if (separator == std::string_view::npos) break;
    remaining.remove_prefix(separator + 1);
  }
  return overrides;
}

GraphConfig load_config(const std::filesystem::path& path,
                        const std::vector<ConfigOverride>& overrides) {
  std::error_code error_code;
  const auto size = std::filesystem::file_size(path, error_code);
  if (error_code)
    throw ConfigError(std::vector<ConfigDiagnostic>{
        {"$", "cannot read '" + path.string() + "': " + error_code.message()}});
  if (size > kMaxConfigBytes)
    throw ConfigError(std::vector<ConfigDiagnostic>{{"$", "configuration exceeds 1 MiB limit"}});
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw ConfigError(std::vector<ConfigDiagnostic>{{"$", "cannot open '" + path.string() + "'"}});
  std::string source((std::istreambuf_iterator<char>(input)), {});
  YAML::Node root;
  try {
    root = YAML::Load(source);
  } catch (const YAML::Exception& error) {
    throw ConfigError(
        std::vector<ConfigDiagnostic>{{"$", "invalid YAML: " + std::string(error.what())}});
  }
  std::vector<ConfigDiagnostic> override_errors;
  for (const auto& override : environment_overrides())
    apply_override(root, override, override_errors);
  for (const auto& override : overrides) apply_override(root, override, override_errors);
  if (!override_errors.empty()) throw ConfigError(std::move(override_errors));
  return ConfigParser(std::move(root)).parse();
}

std::string_view to_string(TransportKind kind) noexcept {
  switch (kind) {
    case TransportKind::in_process:
      return "in_process";
    case TransportKind::tcp:
      return "tcp";
    case TransportKind::unix_socket:
      return "unix";
    case TransportKind::shared_memory:
      return "shared_memory";
  }
  return "unknown";
}

}  // namespace graphx
