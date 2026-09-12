#include "graphx/config.hpp"
#include "graphx/framing.hpp"
#include "config_internal.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace graphx {
namespace {

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
  if (parts.size() == 1 && parts.front() == "version") {
    errors.push_back({override.path,
                      "configuration version is immutable and must come from the source document"});
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

namespace {

GraphConfig load_config_with_overrides(const std::filesystem::path& path,
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
  for (const auto& override : overrides) apply_override(root, override, override_errors);
  if (!override_errors.empty()) throw ConfigError(std::move(override_errors));
  return config_internal::ConfigParser(std::move(root)).parse();
}

}  // namespace

GraphConfig load_config(const std::filesystem::path& path,
                        const std::vector<ConfigOverride>& overrides) {
  auto combined = environment_overrides();
  combined.insert(combined.end(), overrides.begin(), overrides.end());
  return load_config_with_overrides(path, combined);
}

GraphConfig load_config_literal(const std::filesystem::path& path) {
  return load_config_with_overrides(path, {});
}

std::string_view to_string(TransportKind kind) noexcept {
  switch (kind) {
    case TransportKind::in_process:
      return "in_process";
    case TransportKind::tcp:
      return "tcp";
    case TransportKind::udp:
      return "udp";
    case TransportKind::unix_socket:
      return "unix";
    case TransportKind::shared_memory:
      return "shared_memory";
  }
  return "unknown";
}

std::string_view to_string(UdpMode mode) noexcept {
  switch (mode) {
    case UdpMode::unicast:
      return "unicast";
    case UdpMode::broadcast:
      return "broadcast";
    case UdpMode::multicast:
      return "multicast";
  }
  return "unknown";
}

TransportKind transport_kind(const TransportSettings& transport) {
  return std::visit(
      [](const auto& settings) -> TransportKind {
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, TcpTransportConfig>)
          return TransportKind::tcp;
        else if constexpr (std::is_same_v<Settings, UdpTransportConfig>)
          return TransportKind::udp;
        else if constexpr (std::is_same_v<Settings, UnixSocketTransportConfig>)
          return TransportKind::unix_socket;
        else if constexpr (std::is_same_v<Settings, InProcessTransportConfig>)
          return TransportKind::in_process;
        else if constexpr (std::is_same_v<Settings, SharedMemoryTransportConfig>)
          return TransportKind::shared_memory;
        else
          return std::visit(
              [](const auto& protocol) {
                using Protocol = std::decay_t<decltype(protocol)>;
                return std::is_same_v<Protocol, TcpTransportConfig> ? TransportKind::tcp
                                                                    : TransportKind::udp;
              },
              settings.protocol);
      },
      transport);
}

std::string_view transport_framing(const TransportSettings& transport) {
  return std::visit(
      [](const auto& settings) -> std::string_view {
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, InProcessTransportConfig> ||
                      std::is_same_v<Settings, SharedMemoryTransportConfig>)
          return "u32be";
        else if constexpr (std::is_same_v<Settings, ExternalTransportConfig>)
          return std::visit(
              [](const auto& protocol) -> std::string_view { return protocol.framing; },
              settings.protocol);
        else
          return settings.framing;
      },
      transport);
}

}  // namespace graphx
