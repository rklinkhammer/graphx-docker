#include "graphx/ownership.hpp"

#include "infra/lifecycle_coordinator.hpp"

namespace graphx {

int execute_ovs_lifecycle(const GraphConfig& config, const std::filesystem::path& config_path,
                          OvsLifecycleAction action, bool dry_run,
                          const std::filesystem::path& state_root, std::ostream& output,
                          std::ostream& errors) {
  return infra::detail::execute_ovs_lifecycle_impl(config, config_path, action, dry_run, state_root,
                                                   output, errors);
}

int export_owned_network_capture(const GraphConfig& config,
                                 const std::filesystem::path& config_path,
                                 std::string_view capture_id,
                                 const std::filesystem::path& state_root,
                                 const std::filesystem::path& destination, std::ostream& output) {
  return infra::detail::export_owned_network_capture_impl(config, config_path, capture_id,
                                                          state_root, destination, output);
}

std::filesystem::path default_ownership_state_root() {
  return infra::detail::default_ownership_state_root_impl();
}

}  // namespace graphx
