#pragma once

#include "graphx/config.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace graphx::config_internal {

std::optional<std::string> interpret_network_profile(std::string_view profile,
                                                     NetworkDefinition& network);
std::optional<std::string> validate_network_profile(const NetworkDefinition& network);

}  // namespace graphx::config_internal
