#include "graphx/config.hpp"
#include "graphx/normalized_config.hpp"
#include "graphx/version.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {
void usage(std::ostream& out) {
  out << "usage:\n"
      << "  graphx --version\n"
      << "  graphx <validate|inspect> [graphx.yml] [--target TARGET] [--catalog-root DIR]\n"
      << "  graphx config normalize [graphx.yml] [--format json] [--target TARGET]\n"
      << "                          [--catalog-root DIR]\n"
      << "Targets: native-linux (validation default), native-macos, orbstack, lima\n"
      << "Graph execution and artifact generation are unavailable in the P1 model cutover.\n";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 &&
      (std::string_view(argv[1]) == "--version" || std::string_view(argv[1]) == "version")) {
    std::cout << "graphx " << graphx::version << '\n';
    return 0;
  }
  if (argc < 2 || std::string_view(argv[1]) == "--help") {
    usage(argc < 2 ? std::cerr : std::cout);
    return argc < 2 ? 64 : 0;
  }
  try {
    const std::string command = argv[1];
    if (command == "infra" || command == "compile" || command == "run") {
      std::cerr << "E_PHASE_UNAVAILABLE: version 3 execution/artifact adapters are not "
                   "implemented; no action was performed\n";
      return 2;
    }
    int first = 2;
    const bool normalize = command == "config";
    if (normalize) {
      if (argc < 3 || std::string_view(argv[2]) != "normalize")
        throw std::invalid_argument("config requires normalize");
      first = 3;
    } else if (command != "validate" && command != "inspect")
      throw std::invalid_argument("unknown command");
    std::filesystem::path path = "graphx.yml";
    graphx::ConfigLoadOptions options;
    bool path_set{}, target_set{}, root_set{}, format_set{};
    for (int index = first; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--target" || argument == "--catalog-root" || argument == "--format") {
        if (++index == argc) throw std::invalid_argument(argument + " requires a value");
        if (argument == "--target") {
          if (target_set) throw std::invalid_argument("duplicate --target");
          target_set = true;
          options.target = argv[index];
        } else if (argument == "--catalog-root") {
          if (root_set) throw std::invalid_argument("duplicate --catalog-root");
          root_set = true;
          options.catalog_root = argv[index];
        } else {
          if (!normalize || format_set || std::string_view(argv[index]) != "json")
            throw std::invalid_argument("--format requires one json selection on normalize");
          format_set = true;
        }
      } else if (argument.starts_with("--"))
        throw std::invalid_argument("unsupported option " + argument);
      else if (path_set)
        throw std::invalid_argument("unexpected positional argument");
      else {
        path = argument;
        path_set = true;
      }
    }
    const auto graph = graphx::load_graph(path, options);
    if (normalize || command == "inspect")
      std::cout << graphx::normalize_config_json(graph);
    else
      std::cout << path.string() << ": valid GraphX configuration version 3 (" << graph.nodes.size()
                << " nodes, " << graph.edges.size() << " connections; target " << options.target
                << ")\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
