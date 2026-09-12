#include "projection.hpp"

#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace graphx::cli {
namespace {

YAML::Node port_node(const Port& port) {
  YAML::Node value;
  value["name"] = port.name;
  value["direction"] = port.direction == Direction::input ? "input" : "output";
  value["schema"] = port.schema;
  return value;
}

std::string emit(std::string_view source_display, const YAML::Node& root) {
  YAML::Emitter emitter;
  emitter.SetIndent(2);
  emitter << root;
  if (!emitter.good()) throw std::runtime_error("cannot render YAML projection");
  std::ostringstream output;
  output << "# Generated from " << source_display << " by `graphx project`.\n"
         << "# Non-authoritative view: edit graphx.yaml, then regenerate.\n"
         << emitter.c_str() << '\n';
  return output.str();
}

YAML::Node graph_projection(const GraphConfig& config) {
  YAML::Node root;
  root["graph_id"] = config.id;
  YAML::Node nodes(YAML::NodeType::Sequence);
  for (const auto& node : config.nodes) {
    YAML::Node item;
    item["id"] = node.id;
    item["kind"] = node.kind;
    item["runtime"] = node.runtime;
    item["execution"] = node.execution;
    item["lifecycle"] = node.lifecycle;
    item["control"] = node.control;
    if (!node.accelerator.empty()) item["accelerator"] = node.accelerator;
    if (!node.architecture.empty()) item["architecture"] = node.architecture;
    YAML::Node ports(YAML::NodeType::Sequence);
    for (const auto& port : node.ports) ports.push_back(port_node(port));
    item["ports"] = ports;
    nodes.push_back(item);
  }
  root["nodes"] = nodes;
  YAML::Node edges(YAML::NodeType::Sequence);
  for (const auto& edge : config.edges) {
    YAML::Node item;
    item["id"] = edge.edge.id;
    item["from"] = edge.edge.from_node + "." + edge.edge.from_port;
    item["to"] = edge.edge.to_node + "." + edge.edge.to_port;
    item["transport"] = std::string(to_string(edge.transport.kind));
    item["data_plane"] = edge.data_plane;
    edges.push_back(item);
  }
  root["edges"] = edges;
  return root;
}

YAML::Node transport_projection(const GraphConfig& config) {
  YAML::Node root;
  YAML::Node links(YAML::NodeType::Sequence);
  for (const auto& edge : config.edges) {
    const auto& transport = edge.transport;
    YAML::Node item;
    item["edge"] = edge.edge.id;
    item["type"] = std::string(to_string(transport.kind));
    item["data_plane"] = edge.data_plane;
    item["framing"] = transport.framing;
    switch (transport.kind) {
      case TransportKind::in_process:
        item["channel"] = transport.channel;
        item["capacity"] = transport.capacity;
        item["backpressure"] = transport.backpressure;
        item["send_timeout_ms"] = transport.send_timeout_ms;
        break;
      case TransportKind::tcp: {
        item["host"] = transport.host;
        item["bind"] = transport.bind;
        item["port"] = transport.port;
        item["connect_timeout_ms"] = transport.connect_timeout_ms;
        item["send_timeout_ms"] = transport.send_timeout_ms;
        item["reconnect"] = transport.reconnect;
        YAML::Node retry;
        retry["max_attempts"] = transport.retry_attempts;
        retry["initial_backoff_ms"] = transport.retry_initial_backoff_ms;
        retry["max_backoff_ms"] = transport.retry_max_backoff_ms;
        item["retry"] = retry;
        YAML::Node tls;
        tls["enabled"] = transport.tls_enabled;
        tls["verify_peer"] = transport.tls_verify_peer;
        tls["require_client_certificate"] = transport.tls_require_client_certificate;
        if (!transport.tls_ca_file.empty()) tls["ca_file"] = transport.tls_ca_file;
        if (!transport.tls_certificate_file.empty())
          tls["certificate_file"] = transport.tls_certificate_file;
        if (!transport.tls_private_key_file.empty())
          tls["private_key_file"] = transport.tls_private_key_file;
        if (!transport.tls_server_name.empty()) tls["server_name"] = transport.tls_server_name;
        item["tls"] = tls;
        break;
      }
      case TransportKind::udp:
        item["mode"] = std::string(to_string(transport.udp_mode));
        item["destination"] = transport.destination;
        item["bind"] = transport.bind;
        item["port"] = transport.port;
        if (!transport.interface.empty()) item["interface"] = transport.interface;
        item["ttl"] = transport.ttl;
        item["loopback"] = transport.loopback;
        item["reuse_address"] = transport.reuse_address;
        item["receive_buffer_bytes"] = transport.receive_buffer_bytes;
        item["send_buffer_bytes"] = transport.send_buffer_bytes;
        item["max_datagram_bytes"] = transport.max_datagram_bytes;
        break;
      case TransportKind::unix_socket:
        item["path"] = transport.path;
        item["connect_timeout_ms"] = transport.connect_timeout_ms;
        item["send_timeout_ms"] = transport.send_timeout_ms;
        break;
      case TransportKind::shared_memory:
        item["segment"] = transport.segment;
        item["capacity"] = transport.capacity;
        item["max_message_bytes"] = transport.max_message_bytes;
        item["backpressure"] = transport.backpressure;
        item["connect_timeout_ms"] = transport.connect_timeout_ms;
        item["send_timeout_ms"] = transport.send_timeout_ms;
        break;
    }
    links.push_back(item);
  }
  root["links"] = links;
  return root;
}

YAML::Node network_projection(const GraphConfig& config) {
  YAML::Node root;
  const auto& infrastructure = config.network_infrastructure;
  if (!infrastructure.networks.empty()) {
    YAML::Node networks(YAML::NodeType::Sequence);
    for (const auto& network : infrastructure.networks) {
      YAML::Node item;
      item["id"] = network.id;
      item["profile"] = std::string(to_string(*network.profile));
      YAML::Node subnets(YAML::NodeType::Sequence);
      for (const auto& subnet : network.subnets) subnets.push_back(subnet);
      item["subnets"] = subnets;
      if (!network.gateway.empty()) item["gateway"] = network.gateway;
      if (!network.uplink.empty()) item["uplink"] = network.uplink;
      item["external"] = network.external;
      networks.push_back(item);
    }
    root["networks"] = networks;
  }
  if (!infrastructure.switches.empty()) {
    YAML::Node switches(YAML::NodeType::Sequence);
    for (const auto& network_switch : infrastructure.switches) {
      YAML::Node item;
      item["id"] = network_switch.id;
      item["kind"] = std::string(to_string(network_switch.kind));
      item["datapath"] = network_switch.datapath;
      YAML::Node ports(YAML::NodeType::Sequence);
      for (const auto& port : network_switch.ports) {
        YAML::Node value;
        value["id"] = port.id;
        value["interface"] = port.interface;
        if (!port.peer.empty()) value["peer"] = port.peer;
        if (port.vlan.access_tag) value["access_tag"] = *port.vlan.access_tag;
        if (!port.vlan.trunks.empty()) {
          YAML::Node trunks(YAML::NodeType::Sequence);
          for (const auto trunk : port.vlan.trunks) trunks.push_back(trunk);
          value["trunks"] = trunks;
        }
        ports.push_back(value);
      }
      item["ports"] = ports;
      if (network_switch.mirror) {
        YAML::Node mirror;
        mirror["id"] = network_switch.mirror->id;
        mirror["output_port"] = network_switch.mirror->output_port;
        mirror["select_all"] = network_switch.mirror->select_all;
        item["mirror"] = mirror;
      }
      switches.push_back(item);
    }
    root["switches"] = switches;
  }
  if (!infrastructure.routers.empty()) {
    YAML::Node routers(YAML::NodeType::Sequence);
    for (const auto& router : infrastructure.routers) {
      YAML::Node item;
      item["id"] = router.id;
      item["kind"] = std::string(to_string(router.kind));
      if (!router.namespace_name.empty()) item["namespace"] = router.namespace_name;
      item["forwarding"] = router.forwarding;
      YAML::Node interfaces(YAML::NodeType::Sequence);
      for (const auto& interface : router.interfaces) {
        YAML::Node value;
        value["id"] = interface.id;
        value["network"] = interface.network;
        value["address"] = interface.address;
        value["device"] = interface.device;
        if (!interface.peer.empty()) value["peer"] = interface.peer;
        if (!interface.network_switch.empty()) value["switch"] = interface.network_switch;
        interfaces.push_back(value);
      }
      item["interfaces"] = interfaces;
      if (!router.routes.empty()) {
        YAML::Node routes(YAML::NodeType::Sequence);
        for (const auto& route : router.routes) {
          YAML::Node value;
          value["destination"] = route.destination;
          if (!route.via.empty()) value["via"] = route.via;
          if (!route.device.empty()) value["device"] = route.device;
          if (!route.install_on_create) value["install"] = "manual";
          routes.push_back(value);
        }
        item["routes"] = routes;
      }
      if (!router.policies.empty()) {
        YAML::Node policies(YAML::NodeType::Sequence);
        for (const auto& policy : router.policies) {
          YAML::Node value;
          value["id"] = policy.id;
          value["source"] = policy.source;
          value["destination"] = policy.destination;
          value["action"] = policy.action;
          policies.push_back(value);
        }
        item["policies"] = policies;
      }
      routers.push_back(item);
    }
    root["routers"] = routers;
  }
  if (!infrastructure.attachments.empty()) {
    YAML::Node attachments(YAML::NodeType::Sequence);
    for (const auto& attachment : infrastructure.attachments) {
      YAML::Node item;
      item["id"] = attachment.id;
      item["kind"] = std::string(to_string(attachment.kind));
      item["owner"] = attachment.owner;
      if (!attachment.network.empty()) item["network"] = attachment.network;
      if (!attachment.address.empty()) item["address"] = attachment.address;
      if (!attachment.mac.empty()) item["mac"] = attachment.mac;
      if (!attachment.interface.empty()) item["interface"] = attachment.interface;
      if (!attachment.peer.empty()) item["peer"] = attachment.peer;
      if (!attachment.network_switch.empty()) item["switch"] = attachment.network_switch;
      attachments.push_back(item);
    }
    root["attachments"] = attachments;
  }
  if (!infrastructure.edge_paths.empty()) {
    YAML::Node edge_paths(YAML::NodeType::Sequence);
    for (const auto& edge_path : infrastructure.edge_paths) {
      YAML::Node item;
      item["edge"] = edge_path.edge_id;
      YAML::Node hops(YAML::NodeType::Sequence);
      for (const auto& hop : edge_path.hops) hops.push_back(hop);
      item["hops"] = hops;
      edge_paths.push_back(item);
    }
    root["edge_paths"] = edge_paths;
  }
  return root;
}

YAML::Node deployment_projection(const GraphConfig& config) {
  YAML::Node root;
  if (!config.deployment.network.empty()) root["network"] = config.deployment.network;
  if (!config.deployment.services.empty()) {
    YAML::Node services(YAML::NodeType::Sequence);
    for (const auto& service : config.deployment.services) {
      YAML::Node item;
      item["node"] = service.node_id;
      item["image"] = service.image;
      item["command"] = service.command;
      services.push_back(item);
    }
    root["services"] = services;
  }
  if (!config.deployment.telemetry_service.empty() || config.deployment.telemetry_port != 0) {
    YAML::Node telemetry;
    if (!config.deployment.telemetry_service.empty())
      telemetry["service"] = config.deployment.telemetry_service;
    if (config.deployment.telemetry_port != 0) telemetry["port"] = config.deployment.telemetry_port;
    root["telemetry"] = telemetry;
  }
  return root;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string first_difference(std::string_view expected, std::string_view actual) {
  std::istringstream expected_stream{std::string(expected)};
  std::istringstream actual_stream{std::string(actual)};
  std::string expected_line, actual_line;
  for (std::size_t line = 1;; ++line) {
    const bool have_expected = static_cast<bool>(std::getline(expected_stream, expected_line));
    const bool have_actual = static_cast<bool>(std::getline(actual_stream, actual_line));
    if (!have_expected && !have_actual) return "byte content differs";
    if (have_expected != have_actual || expected_line != actual_line) {
      auto bounded = [](std::string value) {
        constexpr std::size_t limit = 120;
        if (value.size() > limit) value.resize(limit), value += "...";
        return value;
      };
      std::ostringstream message;
      message << "first difference at line " << line << "\n"
              << "  expected: " << (have_expected ? bounded(expected_line) : "<end of file>")
              << "\n  actual:   " << (have_actual ? bounded(actual_line) : "<end of file>");
      return message.str();
    }
  }
}

std::string display_source(const std::filesystem::path& source,
                           const std::filesystem::path& output_dir) {
  std::error_code error;
  auto relative = std::filesystem::relative(std::filesystem::absolute(source),
                                            std::filesystem::absolute(output_dir), error);
  if (error || relative.empty()) return source.filename().generic_string();
  return relative.generic_string();
}

void validate_target(const std::filesystem::path& target) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(target, error);
  if (error && error != std::errc::no_such_file_or_directory)
    throw std::runtime_error("cannot inspect projection target " + target.string() + ": " +
                             error.message());
  if (!error && std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
    throw std::runtime_error("projection target is not a regular file: " + target.string());
}

}  // namespace

std::vector<Projection> render_projections(const GraphConfig& config,
                                           std::string_view source_display) {
  return {{"graph-topology.yaml", emit(source_display, graph_projection(config))},
          {"transport-topology.yaml", emit(source_display, transport_projection(config))},
          {"network-topology.yaml", emit(source_display, network_projection(config))},
          {"deployment-topology.yaml", emit(source_display, deployment_projection(config))}};
}

int project_config(const GraphConfig& config, const std::filesystem::path& source,
                   const std::filesystem::path& output_dir, bool check, std::ostream& output,
                   std::ostream& errors) {
  if (output_dir.empty()) throw std::invalid_argument("--output-dir must not be empty");
  const auto absolute_output = std::filesystem::absolute(output_dir).lexically_normal();
  const auto absolute_source = std::filesystem::absolute(source).lexically_normal();
  if (absolute_output == absolute_output.root_path() || absolute_output == absolute_source)
    throw std::invalid_argument("refusing unsafe projection output directory '" +
                                output_dir.string() + "'");

  std::error_code output_error;
  const auto output_status = std::filesystem::symlink_status(output_dir, output_error);
  if (!output_error && std::filesystem::exists(output_status) &&
      (!std::filesystem::is_directory(output_status) || std::filesystem::is_symlink(output_status)))
    throw std::invalid_argument("projection output path must be a real directory: " +
                                output_dir.string());

  const auto projections = render_projections(config, display_source(source, output_dir));
  if (check) {
    bool stale{};
    for (const auto& projection : projections) {
      const auto target = output_dir / projection.filename;
      std::error_code error;
      const auto status = std::filesystem::symlink_status(target, error);
      if (error || !std::filesystem::exists(status)) {
        errors << target.string() << ": missing\n";
        stale = true;
        continue;
      }
      validate_target(target);
      const auto actual = read_file(target);
      if (actual != projection.content) {
        errors << target.string() << ": stale; " << first_difference(projection.content, actual)
               << '\n';
        stale = true;
      }
    }
    if (stale) {
      errors << "Regenerate with: graphx project " << source.string() << " --output-dir "
             << output_dir.string() << '\n';
      return 1;
    }
    output << "GraphX configuration projections are current in " << output_dir.string() << '\n';
    return 0;
  }

  std::error_code error;
  std::filesystem::create_directories(output_dir, error);
  if (error)
    throw std::runtime_error("cannot create projection output directory " + output_dir.string() +
                             ": " + error.message());
  for (const auto& projection : projections) validate_target(output_dir / projection.filename);

  static std::atomic<unsigned long> sequence{};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::vector<std::pair<std::filesystem::path, std::filesystem::path>> paths;
  try {
    for (const auto& projection : projections) {
      const auto target = output_dir / projection.filename;
      auto temporary = target;
      temporary += ".tmp." + std::to_string(stamp) + "." + std::to_string(sequence.fetch_add(1));
      validate_target(temporary);
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      stream.exceptions(std::ios::badbit | std::ios::failbit);
      stream.write(projection.content.data(),
                   static_cast<std::streamsize>(projection.content.size()));
      stream.close();
      paths.emplace_back(temporary, target);
    }
    for (const auto& [temporary, target] : paths) {
      std::filesystem::rename(temporary, target, error);
      if (error)
        throw std::runtime_error("cannot replace projection " + target.string() + ": " +
                                 error.message());
    }
  } catch (...) {
    for (const auto& [temporary, target] : paths) {
      (void)target;
      std::filesystem::remove(temporary, error);
    }
    throw;
  }
  output << "Generated " << projections.size() << " GraphX configuration projections in "
         << output_dir.string() << '\n';
  return 0;
}

}  // namespace graphx::cli
