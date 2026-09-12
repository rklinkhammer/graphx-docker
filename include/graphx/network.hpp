#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace graphx {

enum class NetworkProfile { ethernet, macvlan, ipvlan_l2, ipvlan_l3, ipvlan_l3s };
enum class AttachmentKind { container_veth, namespace_veth, qemu_tap, external, mirror };
enum class SwitchKind { openvswitch };
enum class RouterKind { linux_namespace, container };

struct NetworkProfileSemantics {
  std::string_view mac_identity;
  std::string_view learning;
  std::string_view filtering;
  std::string_view arp;
  std::string_view broadcast;
  std::string_view multicast;
  std::string_view routing;
  std::string_view isolation;
  std::string_view management;
};

struct VlanMetadata {
  std::optional<std::uint16_t> access_tag;
  std::vector<std::uint16_t> trunks;
};

struct NetworkDefinition {
  std::string id;
  std::optional<NetworkProfile> profile;
  std::vector<std::string> subnets;
  std::string gateway;
  std::string uplink;
  bool external{true};
};

struct RouteDefinition {
  std::string destination;
  std::string via;
  std::string device;
  bool install_on_create{true};
};

struct AttachmentDefinition {
  std::string id;
  AttachmentKind kind{AttachmentKind::external};
  std::string owner;
  std::string network;
  std::string address;
  std::string mac;
  std::string interface;
  std::string peer;
  std::string network_switch;
  std::uint32_t mtu{1500};
  std::uint32_t tap_uid{};
  std::uint32_t tap_gid{};
  std::vector<RouteDefinition> routes;
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

// Network observation is deliberately separate from application USER0
// capture. These definitions describe Ethernet frames emitted by an owned OVS
// mirror attachment and retained on the Linux host/VM native filesystem.
struct NetworkCaptureDefinition {
  std::string id;
  std::string attachment;
  std::string directory;
  std::uint32_t snaplen{65535};
  std::uint64_t max_file_bytes{64ULL * 1024 * 1024};
  std::uint32_t max_files{4};
  std::uint32_t rotation_seconds{300};
  std::uint32_t retention_seconds{86400};
};

struct NetworkFaultDefinition {
  std::string id;
  std::string attachment;
  std::uint32_t delay_ms{};
  std::uint32_t jitter_ms{};
  double loss_percent{};
  std::uint32_t rate_kbit{};
  std::uint32_t duration_seconds{30};
};

struct NetworkInfrastructureConfig {
  std::vector<NetworkDefinition> networks;
  std::vector<SwitchDefinition> switches;
  std::vector<RouterDefinition> routers;
  std::vector<AttachmentDefinition> attachments;
  std::vector<EdgeNetworkPath> edge_paths;
  std::vector<NetworkCaptureDefinition> captures;
  std::vector<NetworkFaultDefinition> faults;

  [[nodiscard]] const NetworkDefinition& network(std::string_view id) const;
  [[nodiscard]] const SwitchDefinition& network_switch(std::string_view id) const;
  [[nodiscard]] const RouterDefinition& router(std::string_view id) const;
  [[nodiscard]] const EdgeNetworkPath& edge_path(std::string_view edge_id) const;
};

[[nodiscard]] std::string_view to_string(NetworkProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(AttachmentKind kind) noexcept;
[[nodiscard]] const NetworkProfileSemantics& profile_semantics(NetworkProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(SwitchKind kind) noexcept;
[[nodiscard]] std::string_view to_string(RouterKind kind) noexcept;

}  // namespace graphx
