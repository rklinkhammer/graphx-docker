#include "graphx/config.hpp"
#include "graphx/runtime_identity.hpp"
#include "graphx/infra.hpp"
#include "graphx/normalized_config.hpp"
#include "graphx/instance_resources.hpp"
#include "graphx/ownership.hpp"
#include "graphx/version.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

std::filesystem::path default_config();

void usage(std::ostream& output) {
  output << "usage:\n"
         << "  graphx --version\n"
         << "  graphx <validate|inspect> [config.yaml] [--set path=value]\n"
         << "  graphx config normalize [config.yaml] [--format json] [--resources] [--set "
            "path=value]\n"
         << "  graphx infra <create|destroy|status|recover> [config.yaml] [--dry-run]\n"
         << "               [--state-dir DIR]\n"
         << "  graphx infra route <apply|clear> [config.yaml] --router ID --destination CIDR\n"
         << "  graphx runtime <activate|retire> CONFIG --identity-file FILE --node ID\n"
         << "                 [--execution-id ID]\n"
         << "  graphx infra capture export [config.yaml] --capture ID --output FILE\n"
         << "                    [--state-dir DIR]\n";
}

int normalization_command(int argc, char** argv) {
  auto source = default_config();
  std::vector<graphx::ConfigOverride> overrides;
  bool source_set{}, format_set{}, resources{};
  for (int index = 3; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--resources") {
      resources = true;
    } else if (argument == "--format") {
      if (format_set) throw std::invalid_argument("--format may be specified only once");
      if (++index == argc) throw std::invalid_argument("--format requires json");
      if (std::string_view(argv[index]) != "json")
        throw std::invalid_argument("normalized configuration format must be json");
      format_set = true;
    } else if (argument == "--set") {
      if (++index == argc) throw std::invalid_argument("--set requires path=value");
      const std::string setting = argv[index];
      const auto equals = setting.find('=');
      if (equals == std::string::npos || equals == 0)
        throw std::invalid_argument("--set requires path=value");
      overrides.push_back({setting.substr(0, equals), setting.substr(equals + 1)});
    } else if (argument.starts_with("--")) {
      throw std::invalid_argument("unknown option '" + argument + "'");
    } else if (!source_set) {
      source = argument;
      source_set = true;
    } else {
      throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
  }
  const auto config = graphx::load_config(source, overrides);
  std::cout << graphx::normalize_config_json(
      resources ? graphx::resolve_instance_resources(config).config : config);
  return 0;
}

int config_command(int argc, char** argv) {
  if (argc < 3) throw std::invalid_argument("config requires normalize");
  const std::string_view action = argv[2];
  if (action == "normalize") return normalization_command(argc, argv);
  throw std::invalid_argument("unknown config action '" + std::string(action) + "'");
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
    std::cout << "node " << node.id << " kind=" << node.kind << " runtime=" << node.runtime
              << " execution=" << node.execution << " lifecycle=" << node.lifecycle
              << " control=" << node.control;
    if (!node.accelerator.empty()) std::cout << " accelerator=" << node.accelerator;
    if (!node.architecture.empty()) std::cout << " architecture=" << node.architecture;
    std::cout << '\n';
    for (const auto& port : node.ports)
      std::cout << "  port " << port.name << " direction=" << direction(port.direction)
                << " schema=" << port.schema << '\n';
  }
  for (const auto& edge : config.edges) {
    std::cout << "edge " << edge.edge.id << ' ' << edge.edge.from_node << '.' << edge.edge.from_port
              << " -> " << edge.edge.to_node << '.' << edge.edge.to_port
              << " transport=" << to_string(transport_kind(edge.transport))
              << " data-plane=" << edge.data_plane
              << " framing=" << transport_framing(edge.transport);
    const auto inspect_transport = [](const auto& transport) {
      using Settings = std::decay_t<decltype(transport)>;
      if constexpr (std::is_same_v<Settings, graphx::TcpTransportConfig>)
        std::cout << " connect=" << transport.host << ':' << transport.port
                  << " listen=" << transport.bind << ':' << transport.port
                  << " connect-timeout-ms=" << transport.connect_timeout_ms
                  << " send-timeout-ms=" << transport.send_timeout_ms
                  << " retry=" << transport.retry.max_attempts << '/'
                  << transport.retry.initial_backoff_ms << '-' << transport.retry.max_backoff_ms
                  << "ms reconnect=" << (transport.reconnect ? "true" : "false");
      else if constexpr (std::is_same_v<Settings, graphx::UdpTransportConfig>)
        std::cout << " mode=" << to_string(transport.mode)
                  << " destination=" << transport.destination << ':' << transport.port
                  << " bind=" << transport.bind << ':' << transport.port
                  << (transport.interface.empty() ? "" : " interface=" + transport.interface)
                  << " ttl=" << transport.ttl
                  << " loopback=" << (transport.loopback ? "true" : "false")
                  << " reuse-address=" << (transport.reuse_address ? "true" : "false")
                  << " receive-buffer=" << transport.receive_buffer_bytes
                  << " send-buffer=" << transport.send_buffer_bytes
                  << " max-datagram=" << transport.max_datagram_bytes;
      else if constexpr (std::is_same_v<Settings, graphx::UnixSocketTransportConfig>)
        std::cout << " path=" << transport.path;
      else if constexpr (std::is_same_v<Settings, graphx::SharedMemoryTransportConfig>)
        std::cout << " segment=" << transport.segment << " capacity=" << transport.capacity
                  << " max-message=" << transport.max_message_bytes
                  << " backpressure=" << transport.backpressure
                  << " connect-timeout-ms=" << transport.connect_timeout_ms
                  << " send-timeout-ms=" << transport.send_timeout_ms;
      else if constexpr (std::is_same_v<Settings, graphx::InProcessTransportConfig>)
        std::cout << " channel=" << transport.channel;
    };
    std::visit(
        [&inspect_transport](const auto& transport) {
          using Settings = std::decay_t<decltype(transport)>;
          if constexpr (std::is_same_v<Settings, graphx::ExternalTransportConfig>)
            std::visit(inspect_transport, transport.protocol);
          else
            inspect_transport(transport);
        },
        edge.transport);
    if (std::holds_alternative<graphx::ExternalTransportConfig>(edge.transport))
      std::cout << " observed-only=true";
    std::cout << '\n';
  }
  for (const auto& network : config.network_infrastructure.networks) {
    std::cout << "network " << network.id;
    const auto profile = *network.profile;
    const auto& behavior = graphx::profile_semantics(profile);
    std::cout << " backend=ovs profile=" << to_string(profile) << " mac=" << behavior.mac_identity
              << " learning=" << behavior.learning << " filtering=" << behavior.filtering
              << " arp=" << behavior.arp << " broadcast=" << behavior.broadcast
              << " multicast=" << behavior.multicast << " routing=" << behavior.routing
              << " isolation=" << behavior.isolation << " management=" << behavior.management;
    std::cout << " subnets=";
    for (std::size_t index = 0; index < network.subnets.size(); ++index)
      std::cout << (index == 0 ? "" : ",") << network.subnets[index];
    std::cout << (network.gateway.empty() ? "" : " gateway=" + network.gateway)
              << (network.uplink.empty() ? "" : " uplink=" + network.uplink) << '\n';
  }
  for (const auto& attachment : config.network_infrastructure.attachments)
    std::cout << "attachment " << attachment.id << " kind=" << to_string(attachment.kind)
              << " owner=" << attachment.owner
              << (attachment.network.empty() ? "" : " network=" + attachment.network)
              << (attachment.address.empty() ? "" : " address=" + attachment.address)
              << (attachment.mac.empty() ? "" : " mac=" + attachment.mac)
              << (attachment.interface.empty() ? "" : " interface=" + attachment.interface)
              << (attachment.peer.empty() ? "" : " peer=" + attachment.peer)
              << (attachment.network_switch.empty() ? "" : " switch=" + attachment.network_switch)
              << '\n';
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
  std::cout << "observability metrics="
            << (config.observability.metrics.enabled ? "enabled" : "disabled")
            << " tracing=" << (config.observability.tracing.enabled ? "enabled" : "disabled")
            << " telemetry=" << config.observability.telemetry.host << ':'
            << config.observability.telemetry.port
            << " heartbeat=" << config.observability.telemetry.heartbeat_interval_ms << '/'
            << config.observability.telemetry.heartbeat_timeout_ms << "ms"
            << " capture=" << (config.observability.capture.enabled ? "enabled" : "disabled")
            << " provider="
            << (config.observability.capture.provider.empty()
                    ? "none"
                    : config.observability.capture.provider)
            << " directory=" << config.observability.capture.directory
            << " snaplen=" << config.observability.capture.snaplen
            << " max_file_bytes=" << config.observability.capture.max_file_bytes
            << " max_packets=" << config.observability.capture.max_packets << '\n';
  return 0;
}

int infrastructure_command(int argc, char** argv) {
  if (argc < 3) throw std::invalid_argument("infra requires an action");
  const std::string action = argv[2];
  if (action == "capture") {
    if (argc < 4 || std::string_view(argv[3]) != "export")
      throw std::invalid_argument("infra capture requires the export action");
    auto path = default_config();
    auto state_root = graphx::default_ownership_state_root();
    std::string capture, destination;
    bool path_set{};
    for (int index = 4; index < argc; ++index) {
      const std::string argument = argv[index];
      auto value = [&](std::string& target) {
        if (++index == argc) throw std::invalid_argument(argument + " requires a value");
        target = argv[index];
      };
      if (argument == "--capture")
        value(capture);
      else if (argument == "--output")
        value(destination);
      else if (argument == "--state-dir") {
        std::string value_text;
        value(value_text);
        state_root = value_text;
      } else if (!path_set) {
        path = argument;
        path_set = true;
      } else
        throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
    if (capture.empty() || destination.empty())
      throw std::invalid_argument("infra capture export requires --capture and --output");
    const auto config = graphx::load_config(path);
#if !defined(__linux__)
    throw std::runtime_error("network capture export requires Linux VM-native storage");
#else
    return graphx::export_owned_network_capture(config, path, capture, state_root, destination,
                                                std::cout);
#endif
  }
  if (action == "route") {
    if (argc < 4) throw std::invalid_argument("infra route requires apply or clear");
    const bool clear = std::string_view(argv[3]) == "clear";
    if (!clear && std::string_view(argv[3]) != "apply")
      throw std::invalid_argument("infra route action must be apply or clear");
    auto path = default_config();
    std::string router, destination;
    auto state_root = graphx::default_ownership_state_root();
    bool path_set{}, dry_run{};
    for (int index = 4; index < argc; ++index) {
      const std::string argument = argv[index];
      auto value = [&](std::string& target) {
        if (++index == argc) throw std::invalid_argument(argument + " requires a value");
        target = argv[index];
      };
      if (argument == "--router")
        value(router);
      else if (argument == "--state-dir") {
        if (++index == argc) throw std::invalid_argument("--state-dir requires a directory");
        state_root = argv[index];
      } else if (argument == "--destination")
        value(destination);
      else if (argument == "--dry-run")
        dry_run = true;
      else if (!path_set) {
        path = argument;
        path_set = true;
      } else
        throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
    if (router.empty() || destination.empty())
      throw std::invalid_argument("infra route requires --router and --destination");
    const auto config = graphx::load_config(path);
#if !defined(__linux__)
    if (!dry_run)
      throw std::runtime_error("native route changes require Linux; use --dry-run on this host");
#endif
    return graphx::execute_owned_route(config, path, router, destination, clear, dry_run,
                                       state_root, std::cout, std::cerr);
  }
  if (action == "fault")
    throw std::invalid_argument(
        "network faults are declarative; add a bounded network.faults entry to the configuration");
  graphx::OvsLifecycleAction ovs_action;
  if (action == "create") {
    ovs_action = graphx::OvsLifecycleAction::create;
  } else if (action == "destroy") {
    ovs_action = graphx::OvsLifecycleAction::destroy;
  } else if (action == "status") {
    ovs_action = graphx::OvsLifecycleAction::status;
  } else if (action == "recover") {
    ovs_action = graphx::OvsLifecycleAction::recover;
  } else {
    throw std::invalid_argument("unknown infra action '" + action + "'");
  }
  auto path = default_config();
  auto state_root = graphx::default_ownership_state_root();
  bool path_set{}, dry_run{};
  for (int index = 3; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--dry-run")
      dry_run = true;
    else if (argument == "--state-dir") {
      if (++index == argc) throw std::invalid_argument("--state-dir requires a directory");
      state_root = argv[index];
    } else if (!path_set) {
      path = argument;
      path_set = true;
    } else
      throw std::invalid_argument("unexpected argument '" + argument + "'");
  }
  const auto config = graphx::load_config(path);
#if !defined(__linux__)
  if (!dry_run)
    throw std::runtime_error(
        "native infrastructure changes require Linux; use --dry-run or the GraphX Lima VM");
#endif
  return graphx::execute_ovs_lifecycle(config, path, ovs_action, dry_run, state_root, std::cout,
                                       std::cerr);
}

int runtime_command(int argc, char** argv) {
  if (argc < 4) throw std::invalid_argument("runtime requires activate|retire CONFIG");
  const std::string action = argv[2];
  if (action != "activate" && action != "retire")
    throw std::invalid_argument("unknown runtime action");
  std::string manifest, node, execution;
  for (int index = 4; index < argc; ++index) {
    const std::string option = argv[index];
    if (++index == argc) throw std::invalid_argument("runtime option requires a value");
    if (option == "--identity-file")
      manifest = argv[index];
    else if (option == "--node")
      node = argv[index];
    else if (option == "--execution-id")
      execution = argv[index];
    else
      throw std::invalid_argument("unknown runtime option");
  }
  if (manifest.empty() || node.empty() || (action == "activate" && !execution.empty()))
    throw std::invalid_argument(
        "runtime requires --identity-file and --node; only retire accepts --execution-id");
  const auto identity = graphx::update_runtime_registration(graphx::load_config(argv[3]), manifest,
                                                            node, action == "retire", execution);
  if (!identity.empty()) std::cout << identity << '\n';
  return 0;
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
    if (command == "validate" || command == "inspect") return topology_command(command, argc, argv);
    if (command == "runtime") return runtime_command(argc, argv);
    if (command == "config") return config_command(argc, argv);
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
