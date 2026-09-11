#pragma once

#include "graphx/config.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace graphx::config_internal {

enum class Dialect { v1_compat, v2 };

std::optional<Dialect> dialect_for(std::uint32_t version) noexcept;
bool is_v1(Dialect dialect) noexcept;
bool is_layer_three(const NetworkDefinition& network, Dialect dialect) noexcept;

// These functions own the schema-version-specific semantic vocabulary. YAML
// shape/type diagnostics remain in the common parser.
std::optional<std::string> interpret_v1_network(std::string_view driver, std::string_view mode,
                                                NetworkDefinition& network);
std::optional<std::string> validate_v1_network(const NetworkDefinition& network);

std::optional<std::string> interpret_v2_network(std::string_view profile,
                                                NetworkDefinition& network);
std::optional<std::string> validate_v2_network(const NetworkDefinition& network);

}  // namespace graphx::config_internal
