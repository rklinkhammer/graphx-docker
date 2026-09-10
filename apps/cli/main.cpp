#include "graphx/config.hpp"
#include "graphx/infra.hpp"
#include "graphx/migration.hpp"
#include "graphx/ownership.hpp"
#include "graphx/version.hpp"
#include "projection.hpp"

#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {

std::filesystem::path default_config();

void usage(std::ostream& output) {
  output << "usage:\n"
         << "  graphx --version\n"
         << "  graphx <validate|inspect> [config.yaml] [--set path=value]\n"
         << "  graphx config migrate [config.yaml] [--output FILE]\n"
         << "  graphx project [config.yaml] [--check] [--output-dir DIR]\n"
         << "  graphx infra <create|destroy|status|recover> [config.yaml] [--dry-run]\n"
         << "               [--transactional] [--state-dir DIR]\n"
         << "  graphx infra route <apply|clear> [config.yaml] --router ID --destination CIDR\n"
         << "  graphx infra fault <apply|clear> [config.yaml] --router ID --interface ID\n"
         << "                    [--delay 20ms] [--jitter 3ms] [--loss 1%] [--rate 50mbit]\n";
}

int migration_command(int argc, char** argv) {
  if (argc < 3 || std::string_view(argv[2]) != "migrate")
    throw std::invalid_argument("config requires the 'migrate' action");
  auto source = default_config();
  std::filesystem::path output;
  bool source_set{};
  for (int index = 3; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--output") {
      if (!output.empty()) throw std::invalid_argument("--output may be specified only once");
      if (++index == argc) throw std::invalid_argument("--output requires a file");
      output = argv[index];
    } else if (argument.starts_with("--")) {
      throw std::invalid_argument("unknown option '" + argument + "'");
    } else if (!source_set) {
      source = argument;
      source_set = true;
    } else {
      throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
  }
  const auto migrated = graphx::migrate_config_v1_to_v2(source);
  if (output.empty()) {
    std::cout << migrated;
    return 0;
  }
  if (std::filesystem::absolute(source).lexically_normal() ==
      std::filesystem::absolute(output).lexically_normal())
    throw std::invalid_argument("refusing to overwrite the migration source");
  int descriptor = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (descriptor < 0) {
    if (errno == EEXIST || errno == ELOOP)
      throw std::invalid_argument("refusing to overwrite existing migration output '" +
                                  output.string() + "'");
    throw std::system_error(errno, std::generic_category(),
                            "cannot create migration output '" + output.string() + "'");
  }
  std::size_t written{};
  try {
    while (written < migrated.size()) {
      const auto count = ::write(descriptor, migrated.data() + written, migrated.size() - written);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0)
        throw std::system_error(errno == 0 ? EIO : errno, std::generic_category(),
                                "cannot write migration output '" + output.string() + "'");
      written += static_cast<std::size_t>(count);
    }
    const auto close_result = ::close(descriptor);
    descriptor = -1;
    if (close_result != 0)
      throw std::system_error(errno, std::generic_category(),
                              "cannot close migration output '" + output.string() + "'");
  } catch (...) {
    if (descriptor >= 0) ::close(descriptor);
    ::unlink(output.c_str());
    throw;
  }
  std::cout << "Migrated configuration version 1 to version 2: " << output.string() << '\n';
  return 0;
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
              << " transport=" << to_string(edge.transport.kind)
              << " data-plane=" << edge.data_plane << " framing=" << edge.transport.framing;
    if (edge.transport.kind == graphx::TransportKind::tcp)
      std::cout << " connect=" << edge.transport.host << ':' << edge.transport.port
                << " listen=" << edge.transport.bind << ':' << edge.transport.port
                << " connect-timeout-ms=" << edge.transport.connect_timeout_ms
                << " send-timeout-ms=" << edge.transport.send_timeout_ms
                << " retry=" << edge.transport.retry_attempts << '/'
                << edge.transport.retry_initial_backoff_ms << '-'
                << edge.transport.retry_max_backoff_ms
                << "ms reconnect=" << (edge.transport.reconnect ? "true" : "false");
    else if (edge.transport.kind == graphx::TransportKind::udp)
      std::cout << " mode=" << to_string(edge.transport.udp_mode)
                << " destination=" << edge.transport.destination << ':' << edge.transport.port
                << " bind=" << edge.transport.bind << ':' << edge.transport.port
                << (edge.transport.interface.empty() ? ""
                                                     : " interface=" + edge.transport.interface)
                << " ttl=" << edge.transport.ttl
                << " loopback=" << (edge.transport.loopback ? "true" : "false")
                << " reuse-address=" << (edge.transport.reuse_address ? "true" : "false")
                << " receive-buffer=" << edge.transport.receive_buffer_bytes
                << " send-buffer=" << edge.transport.send_buffer_bytes
                << " max-datagram=" << edge.transport.max_datagram_bytes;
    else if (edge.transport.kind == graphx::TransportKind::unix_socket)
      std::cout << " path=" << edge.transport.path;
    else if (edge.transport.kind == graphx::TransportKind::shared_memory)
      std::cout << " segment=" << edge.transport.segment << " capacity=" << edge.transport.capacity
                << " max-message=" << edge.transport.max_message_bytes
                << " backpressure=" << edge.transport.backpressure
                << " connect-timeout-ms=" << edge.transport.connect_timeout_ms
                << " send-timeout-ms=" << edge.transport.send_timeout_ms;
    else
      std::cout << " channel=" << edge.transport.channel;
    std::cout << '\n';
  }
  for (const auto& network : config.network_infrastructure.networks) {
    std::cout << "network " << network.id;
    if (config.version == 1) {
      std::cout << " driver=" << to_string(network.driver);
    } else {
      const auto profile = *network.profile;
      const auto& behavior = graphx::profile_semantics(profile);
      std::cout << " backend=ovs profile=" << to_string(profile) << " mac=" << behavior.mac_identity
                << " learning=" << behavior.learning << " filtering=" << behavior.filtering
                << " arp=" << behavior.arp << " broadcast=" << behavior.broadcast
                << " multicast=" << behavior.multicast << " routing=" << behavior.routing
                << " isolation=" << behavior.isolation << " management=" << behavior.management;
    }
    std::cout << " subnets=";
    for (std::size_t index = 0; index < network.subnets.size(); ++index)
      std::cout << (index == 0 ? "" : ",") << network.subnets[index];
    std::cout << (network.gateway.empty() ? "" : " gateway=" + network.gateway)
              << (network.parent.empty() ? "" : " parent=" + network.parent)
              << (network.uplink.empty() ? "" : " uplink=" + network.uplink)
              << (network.mode.empty() ? "" : " mode=" + network.mode) << '\n';
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

int project_command(int argc, char** argv) {
  auto path = default_config();
  std::filesystem::path output_dir{"config"};
  bool path_set{}, output_set{}, check{};
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--check") {
      check = true;
    } else if (argument == "--output-dir") {
      if (++index == argc) throw std::invalid_argument("--output-dir requires a directory");
      output_dir = argv[index];
      output_set = true;
    } else if (!path_set) {
      path = argument;
      path_set = true;
    } else {
      throw std::invalid_argument("unexpected argument '" + argument + "'");
    }
  }
  if (!output_set && !std::filesystem::exists(".git"))
    throw std::invalid_argument("--output-dir is required outside a repository checkout");
  const auto config = graphx::load_config(path);
  return graphx::cli::project_config(config, path, output_dir, check, std::cout, std::cerr);
}

int infrastructure_command(int argc, char** argv) {
  if (argc < 3) throw std::invalid_argument("infra requires an action");
  const std::string action = argv[2];
  if (action == "route") {
    if (argc < 4) throw std::invalid_argument("infra route requires apply or clear");
    const bool clear = std::string_view(argv[3]) == "clear";
    if (!clear && std::string_view(argv[3]) != "apply")
      throw std::invalid_argument("infra route action must be apply or clear");
    auto path = default_config();
    std::string router, destination;
    bool path_set{}, dry_run{};
    for (int index = 4; index < argc; ++index) {
      const std::string argument = argv[index];
      auto value = [&](std::string& target) {
        if (++index == argc) throw std::invalid_argument(argument + " requires a value");
        target = argv[index];
      };
      if (argument == "--router")
        value(router);
      else if (argument == "--destination")
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
#if !defined(__linux__)
    if (!dry_run)
      throw std::runtime_error("native route changes require Linux; use --dry-run on this host");
#endif
    const auto config = graphx::load_config(path);
    return graphx::execute_infrastructure_plan(
        {graphx::route_command(config, router, destination, clear)}, dry_run, std::cout, std::cerr);
  }
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
  graphx::OvsLifecycleAction ovs_action;
  if (action == "create") {
    infra_action = graphx::InfraAction::create;
    ovs_action = graphx::OvsLifecycleAction::create;
  } else if (action == "destroy") {
    infra_action = graphx::InfraAction::destroy;
    ovs_action = graphx::OvsLifecycleAction::destroy;
  } else if (action == "status") {
    infra_action = graphx::InfraAction::status;
    ovs_action = graphx::OvsLifecycleAction::status;
  } else if (action == "recover") {
    infra_action = graphx::InfraAction::destroy;
    ovs_action = graphx::OvsLifecycleAction::recover;
  } else {
    throw std::invalid_argument("unknown infra action '" + action + "'");
  }
  auto path = default_config();
  auto state_root = graphx::default_ownership_state_root();
  bool path_set{}, dry_run{}, transactional{};
  for (int index = 3; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--dry-run")
      dry_run = true;
    else if (argument == "--transactional")
      transactional = true;
    else if (argument == "--state-dir") {
      if (++index == argc) throw std::invalid_argument("--state-dir requires a directory");
      state_root = argv[index];
    } else if (!path_set) {
      path = argument;
      path_set = true;
    } else
      throw std::invalid_argument("unexpected argument '" + argument + "'");
  }
  if (transactional && action != "create")
    throw std::invalid_argument("--transactional is supported only for infra create");
#if !defined(__linux__)
  if (!dry_run)
    throw std::runtime_error(
        "native infrastructure changes require Linux; use --dry-run or the macOS OVS lab profile");
#endif
  const auto config = graphx::load_config(path);
  if (config.version == 2) {
    if (transactional)
      throw std::invalid_argument("version 2 create is always transactional; omit --transactional");
    return graphx::execute_ovs_lifecycle(config, path, ovs_action, dry_run, state_root, std::cout,
                                         std::cerr);
  }
  if (action == "recover") throw std::invalid_argument("infra recover requires version 2");
  return graphx::execute_infrastructure_plan(
      graphx::infrastructure_plan(config, infra_action, transactional), dry_run, std::cout,
      std::cerr);
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
    if (command == "config") return migration_command(argc, argv);
    if (command == "project") return project_command(argc, argv);
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
