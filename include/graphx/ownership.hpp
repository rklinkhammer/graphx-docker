#pragma once

#include "graphx/config.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>

namespace graphx {

enum class OvsLifecycleAction { create, destroy, status, recover };

// Stable identity slots shared by later realization milestones. M3 populates
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

// Realizes M3-owned OVS bridges and M4-owned container veth endpoints. Linux
// namespace veths, TAPs, profile flows, faults, and captures remain later milestones.
int execute_ovs_lifecycle(const GraphConfig& config, const std::filesystem::path& config_path,
                          OvsLifecycleAction action, bool dry_run,
                          const std::filesystem::path& state_root, std::ostream& output,
                          std::ostream& errors);

[[nodiscard]] std::filesystem::path default_ownership_state_root();

}  // namespace graphx
