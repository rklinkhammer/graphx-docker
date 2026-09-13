#include "graphx/config.hpp"
#include "graphx/compile.hpp"
#include "graphx/execution.hpp"
#include "graphx/node_settings.hpp"
#include "graphx/normalized_config.hpp"
#include "graphx/ownership.hpp"
#include "graphx/version.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <set>

namespace {
void usage(std::ostream& out) {
  out << "usage:\n"
      << "  graphx --version\n"
      << "  graphx node-settings --node ID --config FILE\n"
      << "  graphx <validate|inspect> [graphx.yml] [--target TARGET] [--catalog-root DIR]\n"
      << "  graphx config normalize [graphx.yml] [--format json] [--target TARGET]\n"
      << "                          [--catalog-root DIR]\n"
      << "  graphx compile FILE --output DIR --source-root DIR --credential-root DIR\n"
      << "                        [--target TARGET] [--catalog-root DIR]\n"
      << "  graphx run <plan|up|status|down> --output DIR --state-root DIR\n"
      << "             [--release DIR --credentials DIR] [--images DIR] [--external DIR]\n"
      << "             [--allow-privileged] (local Linux OVS only)\n"
      << "Targets: native-linux (validation default), native-macos, orbstack, lima\n"
      << "Execution requires verified releases; OVS requires explicit Linux authorization; guests "
         "remain gated.\n";
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
    if (command == "platform-lock") {
      if (argc < 6 || std::string_view(argv[2]) != "--lock" || std::string_view(argv[4]) != "--")
        throw std::invalid_argument("platform-lock requires --lock FILE -- PROGRAM [ARGS]");
      return graphx::execute_with_ownership_lock(argv[3], argv + 5);
    }
    if (command == "node-settings") {
      const auto args = graphx::node_arguments(argc - 1, argv + 1, false);
      std::cout << graphx::config_value_json(
          graphx::load_node_settings(args.config, args.node).resolved);
      return 0;
    }
    if (command == "run") {
      if (argc < 3) throw std::invalid_argument("run requires plan, up, down or status");
      graphx::ExecutionOptions options;
      options.action = argv[2];
      std::map<std::string, std::filesystem::path*> fields{
          {"--output", &options.output},   {"--state-root", &options.state_root},
          {"--release", &options.release}, {"--credentials", &options.credentials},
          {"--images", &options.images},   {"--external", &options.external_credentials}};
      std::set<std::string> seen;
      for (int i = 3; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--owner" && seen.insert(key).second && i + 1 < argc) {
          options.owner_token = argv[++i];
          continue;
        }
        if (key == "--allow-privileged" && seen.insert(key).second) {
          options.allow_privileged = true;
          continue;
        }
        if (!fields.contains(key) || !seen.insert(key).second || i + 1 == argc)
          throw std::invalid_argument("unknown, duplicate or incomplete run option");
        *fields.at(key) = std::filesystem::absolute(argv[++i]).lexically_normal();
      }
      if (options.output.empty() || options.state_root.empty())
        throw std::invalid_argument("run requires --output and --state-root");
      return graphx::execute_graph(options, std::cout);
    }
    if (command == "infra") {
      std::cerr << "E_PHASE_UNAVAILABLE: version 3 execution/artifact adapters are not "
                   "implemented; no action was performed\n";
      return 2;
    }
    if (command == "compile") {
      if (argc < 3) throw std::invalid_argument("compile requires an authored file");
      const std::filesystem::path source = argv[2];
      graphx::ConfigLoadOptions load;
      std::map<std::string, std::string> opts;
      for (int i = 3; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--replace")
          throw std::invalid_argument(
              "E_OUTPUT_OWNERSHIP: replacement is unavailable; use a fresh directory");
        if ((key != "--output" && key != "--source-root" && key != "--credential-root" &&
             key != "--target" && key != "--catalog-root") ||
            i + 1 == argc || !opts.emplace(key, argv[++i]).second)
          throw std::invalid_argument(
              "E_ARGUMENT: unknown, duplicate or incomplete compile option");
      }
      for (const auto* key : {"--output", "--source-root", "--credential-root"})
        if (!opts.contains(key))
          throw std::invalid_argument(std::string("E_ARGUMENT: compile requires ") + key);
      if (opts.contains("--target")) load.target = opts.at("--target");
      if (opts.contains("--catalog-root")) load.catalog_root = opts.at("--catalog-root");
      const auto graph = graphx::load_graph(source, load);
      const auto compiled = graphx::compile_graph(graph);
      graphx::write_compilation(compiled, opts.at("--output"),
                                {graph.input_directory, graph.catalog_directory,
                                 opts.at("--source-root"), opts.at("--credential-root")});
      std::cout << "Compiled " << compiled.files.size()
                << " artifacts; execution requires verified release preflight\n";
      return 0;
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
