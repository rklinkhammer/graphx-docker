#include "graphx/config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
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
    strict_keys(root_, "$", {"version", "graph", "transport", "deployment", "observability"});
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
    parse_deployment(root_["deployment"], config);
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
      else
        error(path + ".transport", "unsupported transport '" + transport + "'");
      config.edges.push_back(std::move(edge));
    }
  }

  void parse_transports(const YAML::Node& transports, GraphConfig& config) {
    if (!require_map(transports, "transport")) return;
    strict_keys(transports, "transport", {"tcp", "unix", "in_process"});
    const std::string_view sections[] = {"tcp", "unix", "in_process"};
    std::unordered_set<std::string> consumed;
    for (auto& edge : config.edges) {
      const auto name = std::string(to_string(edge.transport.kind));
      const auto section = transports[name];
      const auto settings = section ? section[edge.edge.id] : YAML::Node{};
      const auto path = "transport." + name + "." + edge.edge.id;
      if (!require_map(settings, path)) continue;
      consumed.insert(name + "." + edge.edge.id);
      if (edge.transport.kind == TransportKind::tcp) {
        strict_keys(settings, path, {"host", "bind", "port", "framing"});
        edge.transport.host = text(settings["host"], path + ".host", 253);
        edge.transport.bind = text(settings["bind"], path + ".bind", 253);
        const auto port = unsigned_value(settings["port"], path + ".port");
        if (port == 0 || port > 65535)
          error(path + ".port", "must be between 1 and 65535");
        else
          edge.transport.port = static_cast<std::uint16_t>(port);
        if (settings["framing"])
          edge.transport.framing = text(settings["framing"], path + ".framing", 16);
      } else if (edge.transport.kind == TransportKind::unix_socket) {
        strict_keys(settings, path, {"path", "framing"});
        edge.transport.path = text(settings["path"], path + ".path", 103);
        if (settings["framing"])
          edge.transport.framing = text(settings["framing"], path + ".framing", 16);
      } else {
        strict_keys(settings, path, {"channel"});
        edge.transport.channel = text(settings["channel"], path + ".channel", 64);
        identifier(edge.transport.channel, path + ".channel");
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
  }
  return "unknown";
}

}  // namespace graphx
