#pragma once

#include "graphx/config.hpp"
#include <map>

namespace graphx {
inline constexpr std::string_view kCompilerContract = "graphx-compiler-1";
struct CompiledGraph {
  std::string graph_id;
  std::map<std::string, std::string, std::less<>> files;
};
// Pure: no filesystem reads, processes, credential provisioning or infrastructure.
// Input must come from load_graph; all resolved catalog identities are retained.
[[nodiscard]] CompiledGraph compile_graph(const GraphConfig& graph);

struct CompileRoots {
  std::filesystem::path input;
  std::filesystem::path catalog;
  std::filesystem::path source;
  std::filesystem::path credentials;
};
// Exclusive publication only. Existing output (even empty) is never replaced:
// P6 ownership/inactivity evidence is unavailable to the compiler.
void write_compilation(const CompiledGraph& compiled, const std::filesystem::path& output,
                       const CompileRoots& roots);
}  // namespace graphx
