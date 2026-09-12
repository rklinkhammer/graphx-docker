#pragma once

#include "graphx/config.hpp"

namespace graphx {

struct ExecutionIdentity {
  std::string graph_id;
  std::string instance_id;
  std::string execution_id;
};

[[nodiscard]] bool valid_execution_id(std::string_view value) noexcept;
// The trusted launcher assigns this identity before process activation.
void validate_execution_identity(const ExecutionIdentity& identity);
// Atomically updates the existing protected runtime identity manifest. Activate
// refuses an active slot; retire requires its exact current execution identity.
[[nodiscard]] std::string update_runtime_registration(const GraphConfig& config,
                                                      const std::filesystem::path& manifest,
                                                      std::string_view node_id, bool retire,
                                                      std::string_view expected_execution = {});

}  // namespace graphx
