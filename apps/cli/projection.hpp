#pragma once

#include "graphx/config.hpp"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace graphx::cli {

struct Projection {
  std::string filename;
  std::string content;
};

[[nodiscard]] std::vector<Projection> render_projections(const GraphConfig& config,
                                                         std::string_view source_display);

int project_config(const GraphConfig& config, const std::filesystem::path& source,
                   const std::filesystem::path& output_dir, bool check, std::ostream& output,
                   std::ostream& errors);

}  // namespace graphx::cli
