#include "graphx/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(std::ostream& output) {
  output << "usage: graphx <validate|inspect> [config.yaml] [--set path=value]\n";
}

std::string direction(graphx::Direction value) {
  return value == graphx::Direction::input ? "input" : "output";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::string_view(argv[1]) == "--help") {
    usage(argc < 2 ? std::cerr : std::cout);
    return argc < 2 ? 64 : 0;
  }
  const std::string command = argv[1];
  if (command != "validate" && command != "inspect") {
    std::cerr << "graphx: unknown command '" << command << "'\n";
    usage(std::cerr);
    return 64;
  }

  std::filesystem::path path =
      std::getenv("GRAPHX_CONFIG") ? std::getenv("GRAPHX_CONFIG") : "graphx.yaml";
  std::vector<graphx::ConfigOverride> overrides;
  bool path_set{};
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--set") {
      if (++index == argc) {
        std::cerr << "graphx: --set requires path=value\n";
        return 64;
      }
      const std::string setting = argv[index];
      const auto equals = setting.find('=');
      if (equals == std::string::npos || equals == 0) {
        std::cerr << "graphx: --set requires path=value\n";
        return 64;
      }
      overrides.push_back({setting.substr(0, equals), setting.substr(equals + 1)});
    } else if (!path_set) {
      path = argument;
      path_set = true;
    } else {
      std::cerr << "graphx: unexpected argument '" << argument << "'\n";
      return 64;
    }
  }

  try {
    const auto config = graphx::load_config(path, overrides);
    if (command == "validate") {
      std::cout << path.string() << ": valid GraphX configuration version " << config.version
                << " (" << config.nodes.size() << " nodes, " << config.edges.size() << " edges)\n";
      return 0;
    }
    std::cout << "graph " << config.id << " (version " << config.version << ")\n";
    for (const auto& node : config.nodes) {
      std::cout << "node " << node.id << " kind=" << node.kind;
      std::cout << '\n';
      for (const auto& port : node.ports)
        std::cout << "  port " << port.name << " direction=" << direction(port.direction)
                  << " schema=" << port.schema << '\n';
    }
    for (const auto& edge : config.edges) {
      std::cout << "edge " << edge.edge.id << ' ' << edge.edge.from_node << '.'
                << edge.edge.from_port << " -> " << edge.edge.to_node << '.' << edge.edge.to_port
                << " transport=" << to_string(edge.transport.kind);
      if (edge.transport.kind == graphx::TransportKind::tcp)
        std::cout << " connect=" << edge.transport.host << ':' << edge.transport.port
                  << " listen=" << edge.transport.bind << ':' << edge.transport.port;
      else if (edge.transport.kind == graphx::TransportKind::unix_socket)
        std::cout << " path=" << edge.transport.path;
      else
        std::cout << " channel=" << edge.transport.channel;
      std::cout << '\n';
    }
    for (const auto& service : config.deployment.services)
      std::cout << "deployment " << service.node_id << " image=" << service.image
                << " command=" << service.command << '\n';
    return 0;
  } catch (const graphx::ConfigError& error) {
    std::cerr << "graphx: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "graphx: " << error.what() << '\n';
    return 1;
  }
}
