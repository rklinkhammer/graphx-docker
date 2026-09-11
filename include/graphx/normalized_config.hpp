#pragma once

#include "graphx/config.hpp"

#include <cstdint>
#include <string>

namespace graphx {

inline constexpr std::uint32_t kNormalizedConfigContractVersion = 1;

// Returns a deterministic, UTF-8 JSON document describing the fully resolved
// configuration. Credential file contents and environment secrets are never
// read or included.
[[nodiscard]] std::string normalize_config_json(const GraphConfig& config);

}  // namespace graphx
