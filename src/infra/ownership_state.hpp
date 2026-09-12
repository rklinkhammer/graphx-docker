#pragma once

#include "graphx/ownership.hpp"
#include "graphx/instance_resources.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace graphx::infra::detail {

struct ExpectedEndpoint {
  AttachmentKind kind{AttachmentKind::container_veth};
  std::string id;
  std::string owner;
  std::string host_interface;
  std::string target_interface;
  std::string network_switch;
  std::string address;
  std::string mac;
  std::uint32_t mtu{1500};
  std::vector<RouteDefinition> routes;
  std::string container_id;
  std::uint64_t namespace_inode{};
  std::string namespace_name;
  std::uint32_t tap_uid{};
  std::uint32_t tap_gid{};
  VlanMetadata vlan;
};

struct ExpectedCapture {
  NetworkCaptureDefinition definition;
  std::string interface;
};

struct OwnedCapture {
  std::string id;
  std::string attachment_id;
  std::string interface;
  std::filesystem::path session_directory;
  std::uint32_t ifindex{};
  std::uint64_t directory_device{};
  std::uint64_t directory_inode{};
  std::uint32_t directory_uid{};
  std::uint32_t directory_gid{};
  std::uint32_t directory_mode{};
  std::uint32_t pid{};
  std::string process_start_time;
};

struct ExpectedFault {
  NetworkFaultDefinition definition;
  std::string interface;
};

struct OwnedFault {
  std::string id;
  std::string attachment_id;
  std::string interface;
  std::uint32_t ifindex{};
  std::string qdisc_identity;
  std::uint32_t timer_pid{};
  std::string timer_start_time;
  std::string boot_id;
  std::uint64_t applied_monotonic_ns{};
  std::uint64_t expires_monotonic_ns{};
};

struct OwnershipState {
  std::string graph_id;
  std::string instance_id;
  std::vector<InstanceResourceMapping> resource_mappings;
  std::string config_hash;
  std::string owner_token;
  std::string status;
  std::vector<std::string> expected_bridges;
  std::vector<OwnedResourceIdentity> bridges;
  std::vector<ExpectedEndpoint> expected_endpoints;
  std::vector<OwnedResourceIdentity> endpoints;
  std::vector<std::string> expected_namespaces;
  std::vector<OwnedResourceIdentity> namespaces;
  std::vector<ExpectedCapture> expected_captures;
  std::vector<OwnedCapture> captures;
  std::vector<ExpectedFault> expected_faults;
  std::vector<OwnedFault> faults;
};

OwnedResourceIdentity bridge_identity(std::string name, std::string uuid,
                                      std::string internal_port_uuid = {});
bool stable_identity_matches(const OwnedResourceIdentity& expected,
                             const OwnedResourceIdentity& observed);

std::string configuration_hash(const std::filesystem::path& path,
                               const GraphConfig* config = nullptr);
std::string random_token();

void ensure_state_root(const std::filesystem::path& root);
bool inspect_existing_state_root(const std::filesystem::path& root);
bool path_entry_exists(const std::filesystem::path& path);

void save_state(const std::filesystem::path& path, const OwnershipState& state,
                bool replace_existing = true);
OwnershipState load_state(const std::filesystem::path& path);

}  // namespace graphx::infra::detail
