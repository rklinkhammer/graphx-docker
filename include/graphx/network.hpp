#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace graphx {

enum class NetworkDriver { bridge, macvlan, ipvlan };
enum class SwitchKind { openvswitch };
enum class RouterKind { linux_namespace, container };

struct VlanMetadata {
  std::optional<std::uint16_t> access_tag;
  std::vector<std::uint16_t> trunks;
};

struct NetworkDefinition {
  std::string id;
  NetworkDriver driver{NetworkDriver::bridge};
  std::vector<std::string> subnets;
  std::string gateway;
  std::string parent;
  std::string mode;
  bool external{true};
};

struct NetworkInterfaceDefinition {
  std::string id;
  std::string owner;
  std::string network;
  std::string address;
  std::string mac;
};

struct SwitchPortDefinition {
  std::string id;
  std::string interface;
  std::string peer;
  VlanMetadata vlan;
};

struct MirrorDefinition {
  std::string id;
  std::string output_port;
  bool select_all{true};
};

struct SwitchDefinition {
  std::string id;
  SwitchKind kind{SwitchKind::openvswitch};
  std::string datapath{"system"};
  std::vector<SwitchPortDefinition> ports;
  std::optional<MirrorDefinition> mirror;
};

struct RouteDefinition {
  std::string destination;
  std::string via;
  std::string device;
};

struct PolicyDefinition {
  std::string id;
  std::string source;
  std::string destination;
  std::string action{"accept"};
};

struct RouterInterfaceDefinition {
  std::string id;
  std::string network;
  std::string address;
  std::string device;
  std::string peer;
  std::string network_switch;
};

struct RouterDefinition {
  std::string id;
  RouterKind kind{RouterKind::linux_namespace};
  std::string namespace_name;
  bool forwarding{true};
  std::vector<RouterInterfaceDefinition> interfaces;
  std::vector<RouteDefinition> routes;
  std::vector<PolicyDefinition> policies;
};

struct EdgeNetworkPath {
  std::string edge_id;
  std::vector<std::string> hops;
};

struct NetworkInfrastructureConfig {
  std::vector<NetworkDefinition> networks;
  std::vector<SwitchDefinition> switches;
  std::vector<RouterDefinition> routers;
  std::vector<NetworkInterfaceDefinition> interfaces;
  std::vector<EdgeNetworkPath> edge_paths;

  [[nodiscard]] const NetworkDefinition& network(std::string_view id) const;
  [[nodiscard]] const SwitchDefinition& network_switch(std::string_view id) const;
  [[nodiscard]] const RouterDefinition& router(std::string_view id) const;
  [[nodiscard]] const EdgeNetworkPath& edge_path(std::string_view edge_id) const;
};

[[nodiscard]] std::string_view to_string(NetworkDriver driver) noexcept;
[[nodiscard]] std::string_view to_string(SwitchKind kind) noexcept;
[[nodiscard]] std::string_view to_string(RouterKind kind) noexcept;

}  // namespace graphx
