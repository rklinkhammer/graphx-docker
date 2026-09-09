#pragma once

#include <filesystem>
#include <string>

namespace graphx {

// Produces a canonical, reviewable version-2 YAML document without modifying
// the source file. The input is first validated with version-1 semantics.
[[nodiscard]] std::string migrate_config_v1_to_v2(const std::filesystem::path& source);

}  // namespace graphx
