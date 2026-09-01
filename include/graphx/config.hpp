#pragma once

#include "graphx/types.hpp"
#include "graphx/network.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace graphx {

inline constexpr std::uint32_t kConfigVersion = 1;
inline constexpr std::size_t kMaxConfigBytes = 1024 * 1024;
inline constexpr std::size_t kMaxNodes = 1024;
inline constexpr std::size_t kMaxEdges = 4096;
inline constexpr std::size_t kMaxPortsPerNode = 256;

enum class TransportKind { in_process, tcp, unix_socket, shared_memory };

struct TransportConfig {
  TransportKind kind{};
  std::string host;
  std::string bind;
  std::uint16_t port{};
  std::string path;
  std::string channel;
  std::string framing{"u32be"};
  std::uint32_t connect_timeout_ms{5000};
  std::uint32_t send_timeout_ms{5000};
  std::uint32_t retry_attempts{60};
  std::uint32_t retry_initial_backoff_ms{100};
  std::uint32_t retry_max_backoff_ms{2000};
  bool reconnect{true};
  bool tls_enabled{};
  bool tls_verify_peer{true};
  bool tls_require_client_certificate{};
  std::string tls_ca_file;
  std::string tls_certificate_file;
  std::string tls_private_key_file;
  std::string tls_server_name;
  std::string segment;
  std::uint32_t capacity{64};
  std::uint32_t max_message_bytes{1024 * 1024};
  std::string backpressure{"block"};
};

struct NodeConfig {
  std::string id;
  std::string kind;
  std::vector<Port> ports;
};

struct EdgeConfig {
  Edge edge;
  TransportConfig transport;
};

struct DeploymentService {
  std::string node_id;
  std::string image;
  std::string command;
};

struct DeploymentConfig {
  std::string network;
  std::vector<DeploymentService> services;
  std::string telemetry_service;
  std::uint16_t telemetry_port{};
};

struct ObservabilitySignalConfig {
  bool enabled{true};
  std::vector<std::string> exporters{"console", "udp-json"};
};

struct TelemetryConfig {
  std::string host{"127.0.0.1"};
  std::uint16_t port{9000};
  std::string websocket{"/ws"};
  std::uint32_t heartbeat_interval_ms{1000};
  std::uint32_t heartbeat_timeout_ms{5000};
};

struct CaptureConfig {
  bool enabled{};
  std::string provider;
  std::string directory{"captures"};
};

struct ObservabilityConfig {
  ObservabilitySignalConfig metrics;
  ObservabilitySignalConfig tracing;
  TelemetryConfig telemetry;
  CaptureConfig capture;
};

struct GraphConfig {
  std::uint32_t version{};
  std::string id;
  std::vector<NodeConfig> nodes;
  std::vector<EdgeConfig> edges;
  NetworkInfrastructureConfig network_infrastructure;
  DeploymentConfig deployment;
  ObservabilityConfig observability;

  [[nodiscard]] const NodeConfig& node(std::string_view id) const;
  [[nodiscard]] const EdgeConfig& edge(std::string_view id) const;
};

struct ConfigOverride {
  std::string path;
  std::string value;
};

struct ConfigDiagnostic {
  std::string path;
  std::string message;
};

class ConfigError final : public std::runtime_error {
 public:
  explicit ConfigError(std::vector<ConfigDiagnostic> diagnostics);
  [[nodiscard]] const std::vector<ConfigDiagnostic>& diagnostics() const noexcept {
    return diagnostics_;
  }

 private:
  std::vector<ConfigDiagnostic> diagnostics_;
};

// Precedence is file < GRAPHX_OVERRIDES < explicit overrides. Overrides use
// dotted paths, for example transport.tcp.samples.host=127.0.0.1.
[[nodiscard]] std::vector<ConfigOverride> environment_overrides();
[[nodiscard]] GraphConfig load_config(const std::filesystem::path& path,
                                      const std::vector<ConfigOverride>& overrides = {});

[[nodiscard]] std::string_view to_string(TransportKind kind) noexcept;

}  // namespace graphx
