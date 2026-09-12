#pragma once

#include "graphx/config.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace graphx::config_internal {

inline constexpr std::size_t kMaxTextLength = 1024;

extern const std::regex kIdentifier;
extern const std::regex kMacAddress;
extern const std::regex kInterfaceName;
extern const std::regex kHttpOrigin;

struct Ipv4Cidr {
  std::uint32_t network{};
  std::uint32_t mask{};
  unsigned prefix{};
};

std::optional<std::uint32_t> ipv4_address(std::string_view value);
std::optional<Ipv4Cidr> ipv4_cidr(std::string_view value);

class ConfigParser {
 public:
  explicit ConfigParser(YAML::Node root);
  GraphConfig parse();

 private:
  void error(std::string path, std::string message);
  bool require_map(const YAML::Node& node, const std::string& path);
  bool require_sequence(const YAML::Node& node, const std::string& path);
  void strict_keys(const YAML::Node& node, const std::string& path,
                   std::initializer_list<std::string_view> allowed);
  std::string text(const YAML::Node& node, const std::string& path,
                   std::size_t maximum = kMaxTextLength);
  std::string strict_text(const YAML::Node& node, const std::string& path, std::size_t maximum);
  std::uint32_t unsigned_value(const YAML::Node& node, const std::string& path);
  std::uint32_t strict_unsigned_value(const YAML::Node& node, const std::string& path);
  std::uint64_t unsigned_64_value(const YAML::Node& node, const std::string& path);
  std::uint64_t strict_unsigned_64_value(const YAML::Node& node, const std::string& path);
  bool bool_value(const YAML::Node& node, const std::string& path, bool fallback);
  bool strict_bool_value(const YAML::Node& node, const std::string& path, bool fallback);
  double double_value(const YAML::Node& node, const std::string& path, double fallback);
  void identifier(const std::string& value, const std::string& path);

  void parse_nodes(const YAML::Node& nodes, GraphConfig& config);
  SdrConfig parse_sdr(const YAML::Node& value, const std::string& path);
  void validate_sdr(const GraphConfig& config);
  void parse_ports(const YAML::Node& ports, const std::string& path, NodeConfig& node);
  static std::pair<std::string, std::string> endpoint(const std::string& value);
  void parse_edges(const YAML::Node& edges, GraphConfig& config);
  void parse_transports(const YAML::Node& transports, GraphConfig& config);
  VlanMetadata parse_vlan(const YAML::Node& value, const std::string& path);
  void parse_network_infrastructure(const YAML::Node& infrastructure, GraphConfig& config);
  void parse_networks(const YAML::Node& values, GraphConfig& config);
  void parse_switches(const YAML::Node& values, GraphConfig& config);
  void parse_routers(const YAML::Node& values, GraphConfig& config);
  void parse_attachments(const YAML::Node& values, GraphConfig& config);
  void parse_edge_paths(const YAML::Node& paths, GraphConfig& config);
  void parse_network_captures(const YAML::Node& values, GraphConfig& config);
  void parse_network_faults(const YAML::Node& values, GraphConfig& config);
  void parse_deployment(const YAML::Node& deployment, GraphConfig& config);
  void parse_signal(const YAML::Node& value, const std::string& path,
                    ObservabilitySignalConfig& signal, bool allow_otlp);
  void parse_observability(const YAML::Node& value, GraphConfig& config);
  const Port* find_port(const GraphConfig& config, const std::string& node_id,
                        const std::string& port_name, const std::string& path);
  void validate_graph(const GraphConfig& config);
  void validate_network_infrastructure(const GraphConfig& config);

  YAML::Node root_;
  std::vector<ConfigDiagnostic> errors_;
};

std::optional<std::string> interpret_network_profile(std::string_view profile,
                                                     NetworkDefinition& network);
std::optional<std::string> validate_network_profile(const NetworkDefinition& network);

}  // namespace graphx::config_internal
