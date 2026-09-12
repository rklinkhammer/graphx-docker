#pragma once

#include "graphx/config.hpp"

namespace graphx {

struct InstanceResourceMapping {
  std::string kind;
  std::string logical;
  std::string physical;
};

struct InstanceResources {
  GraphConfig config;
  std::string state_key;
  std::vector<InstanceResourceMapping> mappings;
};

// Length-prefixed, case-sensitive identity tuple; no resource ownership is
// inferred from the result. Short kernel names are checked for collisions.
[[nodiscard]] std::string instance_resource_name(std::string_view graph, std::string_view instance,
                                                 std::string_view kind, std::string_view logical);
// Resolves infrastructure only. The input remains the logical configuration.
// Legacy configurations without an instance retain their existing names.
[[nodiscard]] InstanceResources resolve_instance_resources(const GraphConfig& config);

}  // namespace graphx
