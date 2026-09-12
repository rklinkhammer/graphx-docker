#pragma once

#include "graphx/config.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace graphx {

enum class OvsLifecycleAction { create, destroy, status, recover };

// Stable identity slots shared by infrastructure resource types. Bridges populate
// kind/name/stable_id for OVS bridges; later owners populate only the identity
// fields for resources they actually create.
struct OwnedResourceIdentity {
  std::string kind;
  std::string name;
  std::string stable_id;
  std::string secondary_id;
  std::string attachment_id;
  std::string target_interface;
  std::optional<std::uint32_t> ifindex;
  std::optional<std::uint32_t> peer_ifindex;
  std::optional<std::uint64_t> namespace_inode;
  std::string container_id;
  std::string tap_owner;
  std::string route_identity;
  std::string rule_identity;
  std::string qdisc_identity;
  std::string capture_identity;
  std::string process_identity;
};

// Realizes owned OVS bridges, container/namespace veth endpoints, QEMU TAPs,
// Linux router namespaces, OVS SPAN ports, bounded Ethernet captures, and
// timed netem faults.
int execute_ovs_lifecycle(const GraphConfig& config, const std::filesystem::path& config_path,
                          OvsLifecycleAction action, bool dry_run,
                          const std::filesystem::path& state_root, std::ostream& output,
                          std::ostream& errors);

// Copies one identity-checked, complete Ethernet PCAPNG snapshot out of
// VM-native storage. The destination is created exclusively and is never
// followed through a symlink.
int export_owned_network_capture(const GraphConfig& config,
                                 const std::filesystem::path& config_path,
                                 std::string_view capture_id,
                                 const std::filesystem::path& state_root,
                                 const std::filesystem::path& destination, std::ostream& output);

[[nodiscard]] std::filesystem::path default_ownership_state_root();

}  // namespace graphx
