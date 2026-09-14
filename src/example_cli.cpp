#include "graphx/example_cli.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace graphx {
int execute_example_cli(int argc, char** argv) {
  auto executable = std::filesystem::absolute(argv[0]);
  if (!std::filesystem::exists(executable)) {
    // An invocation through PATH still needs an absolute binary for child commands.
    const auto* path = std::getenv("PATH");
    std::string paths = path ? path : "";
    std::size_t begin{};
    while (begin <= paths.size()) {
      const auto end = paths.find(':', begin);
      auto candidate = std::filesystem::path(paths.substr(begin, end - begin)) / argv[0];
      if (std::filesystem::exists(candidate)) {
        executable = std::filesystem::absolute(candidate);
        break;
      }
      if (end == std::string::npos) break;
      begin = end + 1;
    }
  }
  executable = std::filesystem::canonical(executable);
  auto helper = executable.parent_path().parent_path() / "libexec/graphx/example_cli.py";
  if (!std::filesystem::exists(helper)) helper = GRAPHX_EXAMPLE_HELPER;
  std::vector<std::string> arguments{"python3", helper.string(), "--graphx", executable.string()};
  for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
  std::vector<char*> pointers;
  for (auto& argument : arguments) pointers.push_back(argument.data());
  pointers.push_back(nullptr);
  ::execvp(pointers.front(), pointers.data());
  throw std::runtime_error("example workflow requires python3 and the installed CLI helper");
}
}  // namespace graphx
