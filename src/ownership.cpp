#include "graphx/ownership.hpp"

#include "infra/lifecycle_coordinator.hpp"
#include "graphx/infra.hpp"
#include "infra/ownership_lock.hpp"
#include "infra/ownership_state.hpp"
#include "infra/namespace_resources.hpp"

#include <algorithm>
#include <stdexcept>

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

int execute_owned_route(const GraphConfig& logical_config, const std::filesystem::path& config_path,
                        std::string_view router_id, std::string_view destination, bool clear,
                        bool dry_run, const std::filesystem::path& state_root, std::ostream& output,
                        std::ostream& errors) {
  using namespace infra::detail;
  const auto resources = resolve_instance_resources(logical_config);
  const auto& config = resources.config;
  const auto command = route_command(config, router_id, destination, clear);
  if (dry_run) return execute_infrastructure_plan({command}, true, output, errors);
  if (!inspect_existing_state_root(state_root))
    throw std::runtime_error("route operation requires existing ownership state");
  auto lock = OwnershipLock::open_existing(state_root / (resources.state_key + ".lock"),
                                           OwnershipLockMode::exclusive);
  const auto state = load_state(state_root / (resources.state_key + ".yaml"));
  if (state.graph_id != config.id || state.instance_id != config.deployment.instance_id ||
      state.config_hash != configuration_hash(config_path, &logical_config) ||
      state.status != "ready")
    throw std::runtime_error("route operation requires matching ready ownership state");
  const auto& router = config.network_infrastructure.router(router_id);
  const auto found = std::ranges::find_if(
      state.namespaces, [&](const auto& item) { return item.name == router.namespace_name; });
  if (found == state.namespaces.end() || !namespace_owned(*found, state))
    throw std::runtime_error("route namespace is missing or replaced");
  return execute_infrastructure_plan({command}, false, output, errors);
}

std::filesystem::path default_ownership_state_root() {
  return infra::detail::default_ownership_state_root_impl();
}

}  // namespace graphx
