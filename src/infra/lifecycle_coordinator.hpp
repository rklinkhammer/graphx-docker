#pragma once

#include "graphx/config.hpp"
#include "graphx/ownership.hpp"
#include "infra/ownership_state.hpp"
#include "infra/ownership_lock.hpp"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string_view>
#include <set>

namespace graphx::infra::detail {

// The finite graph runner holds the common lock across process and network mutations.
// This context cannot introduce a second owner token or infrastructure ledger.
struct OvsExecutionContext {
  OwnershipState& state;
  const OwnershipLock& lock;
  std::function<bool()> cancelled;
  bool validate_only{};
  std::set<std::string> unavailable_containers;
};

int execute_ovs_lifecycle_impl(const GraphConfig& config, const std::filesystem::path& config_path,
                               OvsLifecycleAction action, bool dry_run,
                               const std::filesystem::path& state_root, std::ostream& output,
                               std::ostream& errors, OvsExecutionContext* context = nullptr);

int export_owned_network_capture_impl(const GraphConfig& config,
                                      const std::filesystem::path& config_path,
                                      std::string_view capture_id,
                                      const std::filesystem::path& state_root,
                                      const std::filesystem::path& destination,
                                      std::ostream& output,
                                      const OwnershipState* locked_state = nullptr);

std::filesystem::path default_ownership_state_root_impl();

}  // namespace graphx::infra::detail
