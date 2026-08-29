#include "graphx/config.hpp"
#include "graphx/infra.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(std::ostream& output) {
  output << "usage:\n"
         << "  graphx <validate|inspect> [config.yaml] [--set path=value]\n"
         << "  graphx infra <create|destroy|status> [config.yaml] [--dry-run]\n"
         << "  graphx infra fault <apply|clear> [config.yaml] --router ID --interface ID\n"
         << "                    [--delay 20ms] [--jitter 3ms] [--loss 1%] [--rate 50mbit]\n";
}

std::string direction(graphx::Direction value) {
  return value == graphx::Direction::input ? "input" : "output";
}

std::filesystem::path default_config() {
  return std::getenv("GRAPHX_CONFIG") ? std::getenv("GRAPHX_CONFIG") : "graphx.yaml";
}

int topology_command(const std::string& command, int argc, char** argv) {
  auto path = default_config();
  std::vector<graphx::ConfigOverride> overrides;
  bool path_set{};
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--set") {
      if (++index == argc) throw std::invalid_argument("--set requires path=value");
      const std::string setting = argv[index];
      const auto equals = setting.find('=');
      if (equals == std::string::npos || equals == 0)
        throw std::invalid_argument("--set requires path=value");
      overrides.push_back({setting.substr(0, equals), setting.substr(equals + 1)});
    } else if (!path_set) {
      path = argument;
      path_set = true;
    } else {
      throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
  }

  const auto config = graphx::load_config(path, overrides);
  if (command == "validate") {
    std::cout << path.string() << ": valid GraphX configuration version " << config.version << " ("
              << config.nodes.size() << " nodes, " << config.edges.size() << " edges, "
              << config.network_infrastructure.networks.size() << " networks)\n";
    return 0;
  }
  std::cout << "graph " << config.id << " (version " << config.version << ")\n";
  for (const auto& node : config.nodes) {
    std::cout << "node " << node.id << " kind=" << node.kind << '\n';
    for (const auto& port : node.ports)
      std::cout << "  port " << port.name << " direction=" << direction(port.direction)
                << " schema=" << port.schema << '\n';
  }
  for (const auto& edge : config.edges) {
    std::cout << "edge " << edge.edge.id << ' ' << edge.edge.from_node << '.' << edge.edge.from_port
              << " -> " << edge.edge.to_node << '.' << edge.edge.to_port
              << " transport=" << to_string(edge.transport.kind);
    if (edge.transport.kind == graphx::TransportKind::tcp)
      std::cout << " connect=" << edge.transport.host << ':' << edge.transport.port
                << " listen=" << edge.transport.bind << ':' << edge.transport.port
                << " connect-timeout-ms=" << edge.transport.connect_timeout_ms
                << " send-timeout-ms=" << edge.transport.send_timeout_ms
                << " retry=" << edge.transport.retry_attempts << '/'
                << edge.transport.retry_initial_backoff_ms << '-'
                << edge.transport.retry_max_backoff_ms << "ms reconnect="
                << (edge.transport.reconnect ? "true" : "false");
    else if (edge.transport.kind == graphx::TransportKind::unix_socket)
      std::cout << " path=" << edge.transport.path;
    else
      std::cout << " channel=" << edge.transport.channel;
    std::cout << '\n';
  }
  for (const auto& network : config.network_infrastructure.networks)
    std::cout << "network " << network.id << " driver=" << to_string(network.driver)
              << " subnet=" << network.subnet
              << (network.gateway.empty() ? "" : " gateway=" + network.gateway)
              << (network.parent.empty() ? "" : " parent=" + network.parent)
              << (network.mode.empty() ? "" : " mode=" + network.mode) << '\n';
  for (const auto& network_switch : config.network_infrastructure.switches)
    std::cout << "switch " << network_switch.id << " kind=" << to_string(network_switch.kind)
              << " datapath=" << network_switch.datapath << " ports=" << network_switch.ports.size()
              << (network_switch.mirror ? " mirror=" + network_switch.mirror->id : "") << '\n';
  for (const auto& router : config.network_infrastructure.routers)
    std::cout << "router " << router.id << " kind=" << to_string(router.kind)
              << " interfaces=" << router.interfaces.size()
              << " forwarding=" << (router.forwarding ? "true" : "false") << '\n';
  for (const auto& path_value : config.network_infrastructure.edge_paths) {
    std::cout << "network-path " << path_value.edge_id;
    for (const auto& hop : path_value.hops) std::cout << " -> " << hop;
    std::cout << '\n';
  }
  for (const auto& service : config.deployment.services)
    std::cout << "deployment " << service.node_id << " image=" << service.image
              << " command=" << service.command << '\n';
  return 0;
}

int infrastructure_command(int argc, char** argv) {
  if (argc < 3) throw std::invalid_argument("infra requires an action");
  const std::string action = argv[2];
  if (action == "fault") {
    if (argc < 4) throw std::invalid_argument("infra fault requires apply or clear");
    const bool clear = std::string_view(argv[3]) == "clear";
    if (!clear && std::string_view(argv[3]) != "apply")
      throw std::invalid_argument("infra fault action must be apply or clear");
    auto path = default_config();
    std::string router, interface, delay, jitter, loss, rate;
    bool path_set{}, dry_run{};
    for (int index = 4; index < argc; ++index) {
      const std::string argument = argv[index];
      auto value = [&](std::string& destination) {
        if (++index == argc) throw std::invalid_argument(argument + " requires a value");
        destination = argv[index];
      };
      if (argument == "--router")
        value(router);
      else if (argument == "--interface")
        value(interface);
      else if (argument == "--delay")
        value(delay);
      else if (argument == "--jitter")
        value(jitter);
      else if (argument == "--loss")
        value(loss);
      else if (argument == "--rate")
        value(rate);
      else if (argument == "--dry-run")
        dry_run = true;
      else if (!path_set) {
        path = argument;
        path_set = true;
      } else
        throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
    if (router.empty() || interface.empty())
      throw std::invalid_argument("infra fault requires --router and --interface");
#if !defined(__linux__)
    if (!dry_run)
      throw std::runtime_error(
          "native fault injection requires Linux; use --dry-run or the mixed-network helper");
#endif
    const auto config = graphx::load_config(path);
    return graphx::execute_infrastructure_plan(
        {graphx::netem_command(config, router, interface, clear, delay, jitter, loss, rate)},
        dry_run, std::cout, std::cerr);
  }

  graphx::InfraAction infra_action;
  if (action == "create")
    infra_action = graphx::InfraAction::create;
  else if (action == "destroy")
    infra_action = graphx::InfraAction::destroy;
  else if (action == "status")
    infra_action = graphx::InfraAction::status;
  else
    throw std::invalid_argument("unknown infra action '" + action + "'");
  auto path = default_config();
  bool path_set{}, dry_run{};
  for (int index = 3; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--dry-run")
      dry_run = true;
    else if (!path_set) {
      path = argument;
      path_set = true;
    } else
      throw std::invalid_argument("unexpected argument '" + argument + "'");
  }
#if !defined(__linux__)
  if (!dry_run)
    throw std::runtime_error(
        "native infrastructure changes require Linux; use --dry-run or the macOS OVS lab profile");
#endif
  const auto config = graphx::load_config(path);
  return graphx::execute_infrastructure_plan(graphx::infrastructure_plan(config, infra_action),
                                             dry_run, std::cout, std::cerr);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::string_view(argv[1]) == "--help") {
    usage(argc < 2 ? std::cerr : std::cout);
    return argc < 2 ? 64 : 0;
  }
  try {
    const std::string command = argv[1];
    if (command == "validate" || command == "inspect") return topology_command(command, argc, argv);
    if (command == "infra") return infrastructure_command(argc, argv);
    throw std::invalid_argument("unknown command '" + command + "'");
  } catch (const graphx::ConfigError& error) {
    std::cerr << "graphx: " << error.what() << '\n';
    return 2;
  } catch (const std::invalid_argument& error) {
    std::cerr << "graphx: " << error.what() << '\n';
    usage(std::cerr);
    return 64;
  } catch (const std::exception& error) {
    std::cerr << "graphx: " << error.what() << '\n';
    return 1;
  }
}
