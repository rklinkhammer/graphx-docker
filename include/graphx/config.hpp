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

enum class TransportKind { in_process, tcp, unix_socket };

struct TransportConfig {
  TransportKind kind{};
  std::string host;
  std::string bind;
  std::uint16_t port{};
  std::string path;
  std::string channel;
  std::string framing{"u32be"};
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

struct GraphConfig {
  std::uint32_t version{};
  std::string id;
  std::vector<NodeConfig> nodes;
  std::vector<EdgeConfig> edges;
  NetworkInfrastructureConfig network_infrastructure;
  DeploymentConfig deployment;

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
