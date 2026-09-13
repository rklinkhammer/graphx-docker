#pragma once
#include <filesystem>
#include <iosfwd>
#include <string>

namespace graphx {
struct ExecutionOptions {
  std::string action;
  std::filesystem::path output;
  std::filesystem::path state_root;
  std::filesystem::path release;
  std::filesystem::path credentials;
  std::filesystem::path images;
  std::filesystem::path external_credentials;
};
int execute_graph(const ExecutionOptions& options, std::ostream& output);
}  // namespace graphx
