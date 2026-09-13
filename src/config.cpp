#include "graphx/config.hpp"

#include <algorithm>
#include <sstream>
#include <type_traits>

namespace graphx {
namespace {

std::string diagnostics_message(const std::vector<ConfigDiagnostic>& diagnostics) {
  std::ostringstream message;
  message << "configuration validation failed with " << diagnostics.size() << " error";
  if (diagnostics.size() != 1) message << 's';
  for (const auto& diagnostic : diagnostics)
    message << "\n- " << diagnostic.code << " " << diagnostic.path << ": " << diagnostic.message;
  return message.str();
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

std::vector<ConfigOverride> environment_overrides() { return {}; }

GraphConfig load_config(const std::filesystem::path& path,
                        const std::vector<ConfigOverride>& overrides) {
  if (!overrides.empty())
    throw ConfigError(
        {{"overrides", "version 3 requires authored settings; overrides are unsupported",
          "E_OVERRIDE"}});
  return load_graph(path);
}

GraphConfig load_config_literal(const std::filesystem::path& path) { return load_graph(path); }

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
