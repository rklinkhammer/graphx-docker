#pragma once

#include "graphx/config.hpp"
#include "graphx/ownership.hpp"

#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace graphx::infra::detail {

int execute_ovs_lifecycle_impl(const GraphConfig& config, const std::filesystem::path& config_path,
                               OvsLifecycleAction action, bool dry_run,
                               const std::filesystem::path& state_root, std::ostream& output,
                               std::ostream& errors);

int export_owned_network_capture_impl(const GraphConfig& config,
                                      const std::filesystem::path& config_path,
                                      std::string_view capture_id,
                                      const std::filesystem::path& state_root,
                                      const std::filesystem::path& destination,
                                      std::ostream& output);

std::filesystem::path default_ownership_state_root_impl();

}  // namespace graphx::infra::detail
