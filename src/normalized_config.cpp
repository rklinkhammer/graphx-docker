#include "graphx/normalized_config.hpp"

#include <array>
#include <charconv>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace graphx {
namespace {

struct Json {
  enum class Kind { null, boolean, unsigned_integer, real, string, array, object };

  Kind kind{Kind::null};
  bool boolean_value{};
  std::uint64_t unsigned_value{};
  double real_value{};
  std::string member_name;
  std::string string_value;
  std::vector<Json> array_value;
  std::vector<Json> object_value;

  static Json null() { return {}; }
  static Json boolean(bool value) {
    Json result;
    result.kind = Kind::boolean;
    result.boolean_value = value;
    return result;
  }
  static Json number(std::uint64_t value) {
    Json result;
    result.kind = Kind::unsigned_integer;
    result.unsigned_value = value;
    return result;
  }
  static Json real(double value) {
    Json result;
    result.kind = Kind::real;
    result.real_value = value;
    return result;
  }
  static Json text(std::string_view value) {
    Json result;
    result.kind = Kind::string;
    result.string_value = value;
    return result;
  }
  static Json array(std::vector<Json> values) {
    Json result;
    result.kind = Kind::array;
    result.array_value = std::move(values);
    return result;
  }
  static Json object(std::initializer_list<std::pair<std::string, Json>> values);
};

Json Json::object(std::initializer_list<std::pair<std::string, Json>> values) {
  Json result;
  result.kind = Kind::object;
  result.object_value.reserve(values.size());
  for (const auto& [name, value] : values) {
    auto member = value;
    member.member_name = name;
    result.object_value.push_back(std::move(member));
  }
  return result;
}

std::string escaped(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (character < 0x20) {
          result += "\\u00";
          result.push_back(hex[character >> 4]);
          result.push_back(hex[character & 0x0f]);
        } else {
          result.push_back(static_cast<char>(character));
        }
    }
  }
  result.push_back('"');
  return result;
}

void render(const Json& value, std::string& output, std::size_t indentation) {
  const auto indent = [&output](std::size_t size) { output.append(size, ' '); };
  switch (value.kind) {
    case Json::Kind::null:
      output += "null";
      return;
    case Json::Kind::boolean:
      output += value.boolean_value ? "true" : "false";
      return;
    case Json::Kind::unsigned_integer:
      output += std::to_string(value.unsigned_value);
      return;
    case Json::Kind::real: {
      std::array<char, 64> buffer{};
      const auto result =
          std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.real_value);
      if (result.ec != std::errc{}) throw std::runtime_error("cannot serialize JSON number");
      output.append(buffer.data(), result.ptr);
      return;
    }
    case Json::Kind::string:
      output += escaped(value.string_value);
      return;
    case Json::Kind::array:
      if (value.array_value.empty()) {
        output += "[]";
        return;
      }
      output += "[\n";
      for (std::size_t index = 0; index < value.array_value.size(); ++index) {
        indent(indentation + 2);
        render(value.array_value[index], output, indentation + 2);
        output += index + 1 == value.array_value.size() ? "\n" : ",\n";
      }
      indent(indentation);
      output += ']';
      return;
    case Json::Kind::object:
      if (value.object_value.empty()) {
        output += "{}";
        return;
      }
      output += "{\n";
      for (std::size_t index = 0; index < value.object_value.size(); ++index) {
        const auto& child = value.object_value[index];
        indent(indentation + 2);
        output += escaped(child.member_name);
        output += ": ";
        render(child, output, indentation + 2);
        output += index + 1 == value.object_value.size() ? "\n" : ",\n";
      }
      indent(indentation);
      output += '}';
      return;
  }
}

template <typename Range, typename Convert>
Json converted_array(const Range& values, Convert convert) {
  std::vector<Json> result;
  result.reserve(values.size());
  for (const auto& value : values) result.push_back(convert(value));
  return Json::array(std::move(result));
}

Json string_array(const std::vector<std::string>& values) {
  return converted_array(values, [](const std::string& value) { return Json::text(value); });
}

Json port_json(const Port& port) {
  return Json::object({
      {"name", Json::text(port.name)},
      {"direction", Json::text(port.direction == Direction::input ? "input" : "output")},
      {"schema", Json::text(port.schema)},
  });
}

Json node_json(const NodeConfig& node) {
  auto result = Json::object({
      {"id", Json::text(node.id)},
      {"kind", Json::text(node.kind)},
      {"runtime", Json::text(node.runtime)},
      {"execution", Json::text(node.execution)},
      {"lifecycle", Json::text(node.lifecycle)},
      {"control", Json::text(node.control)},
      {"accelerator", node.accelerator.empty() ? Json::null() : Json::text(node.accelerator)},
      {"architecture", node.architecture.empty() ? Json::null() : Json::text(node.architecture)},
      {"ports", converted_array(node.ports, port_json)},
  });
  if (node.sdr) {
    const auto& sdr = *node.sdr;
    auto value = Json::object({
        {"samples_edge", Json::text(sdr.samples_edge)},
        {"control_edge", Json::text(sdr.control_edge)},
        {"frequency_hz", Json::number(sdr.frequency_hz)},
        {"sample_interval_ms", Json::number(sdr.sample_interval_ms)},
        {"credentials", Json::object({
                            {"ca_file", Json::text(sdr.credentials.ca_file)},
                            {"certificate_file", Json::text(sdr.credentials.certificate_file)},
                            {"private_key_file", Json::text(sdr.credentials.private_key_file)},
                            {"server_name", Json::text(sdr.credentials.server_name)},
                        })},
    });
    value.member_name = "sdr";
    result.object_value.push_back(std::move(value));
  }
  return result;
}

Json transport_json(const InProcessTransportConfig& transport) {
  return Json::object({{"kind", Json::text("in_process")},
                       {"channel", Json::text(transport.channel)},
                       {"capacity", Json::number(transport.capacity)},
                       {"backpressure", Json::text(transport.backpressure)},
                       {"send_timeout_ms", Json::number(transport.send_timeout_ms)}});
}

Json transport_json(const TcpTransportConfig& transport) {
  return Json::object({
      {"kind", Json::text("tcp")},
      {"host", Json::text(transport.host)},
      {"bind", Json::text(transport.bind)},
      {"port", Json::number(transport.port)},
      {"framing", Json::text(transport.framing)},
      {"connect_timeout_ms", Json::number(transport.connect_timeout_ms)},
      {"send_timeout_ms", Json::number(transport.send_timeout_ms)},
      {"reconnect", Json::boolean(transport.reconnect)},
      {"retry",
       Json::object({{"max_attempts", Json::number(transport.retry.max_attempts)},
                     {"initial_backoff_ms", Json::number(transport.retry.initial_backoff_ms)},
                     {"max_backoff_ms", Json::number(transport.retry.max_backoff_ms)}})},
      {"tls",
       Json::object({
           {"enabled", Json::boolean(transport.tls.enabled)},
           {"verify_peer", Json::boolean(transport.tls.verify_peer)},
           {"require_client_certificate", Json::boolean(transport.tls.require_client_certificate)},
           {"ca_file",
            transport.tls.ca_file.empty() ? Json::null() : Json::text(transport.tls.ca_file)},
           {"certificate_file", transport.tls.certificate_file.empty()
                                    ? Json::null()
                                    : Json::text(transport.tls.certificate_file)},
           {"private_key_file", transport.tls.private_key_file.empty()
                                    ? Json::null()
                                    : Json::text(transport.tls.private_key_file)},
           {"server_name", transport.tls.server_name.empty()
                               ? Json::null()
                               : Json::text(transport.tls.server_name)},
       })},
  });
}

Json transport_json(const UdpTransportConfig& transport) {
  return Json::object({
      {"kind", Json::text("udp")},
      {"mode", Json::text(to_string(transport.mode))},
      {"destination", Json::text(transport.destination)},
      {"bind", Json::text(transport.bind)},
      {"port", Json::number(transport.port)},
      {"interface", transport.interface.empty() ? Json::null() : Json::text(transport.interface)},
      {"ttl", Json::number(transport.ttl)},
      {"loopback", Json::boolean(transport.loopback)},
      {"reuse_address", Json::boolean(transport.reuse_address)},
      {"receive_buffer_bytes", Json::number(transport.receive_buffer_bytes)},
      {"send_buffer_bytes", Json::number(transport.send_buffer_bytes)},
      {"max_datagram_bytes", Json::number(transport.max_datagram_bytes)},
      {"framing", Json::text(transport.framing)},
  });
}

Json transport_json(const UnixSocketTransportConfig& transport) {
  return Json::object({{"kind", Json::text("unix")},
                       {"path", Json::text(transport.path)},
                       {"framing", Json::text(transport.framing)},
                       {"connect_timeout_ms", Json::number(transport.connect_timeout_ms)},
                       {"send_timeout_ms", Json::number(transport.send_timeout_ms)}});
}

Json transport_json(const SharedMemoryTransportConfig& transport) {
  return Json::object({{"kind", Json::text("shared_memory")},
                       {"segment", Json::text(transport.segment)},
                       {"capacity", Json::number(transport.capacity)},
                       {"max_message_bytes", Json::number(transport.max_message_bytes)},
                       {"backpressure", Json::text(transport.backpressure)},
                       {"connect_timeout_ms", Json::number(transport.connect_timeout_ms)},
                       {"send_timeout_ms", Json::number(transport.send_timeout_ms)}});
}

Json transport_json(const TransportSettings& transport) {
  return std::visit(
      [](const auto& settings) {
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, ExternalTransportConfig>)
          return std::visit([](const auto& protocol) { return transport_json(protocol); },
                            settings.protocol);
        else
          return transport_json(settings);
      },
      transport);
}

Json edge_json(const EdgeConfig& edge) {
  return Json::object({
      {"id", Json::text(edge.edge.id)},
      {"from", Json::object({{"node", Json::text(edge.edge.from_node)},
                             {"port", Json::text(edge.edge.from_port)}})},
      {"to", Json::object({{"node", Json::text(edge.edge.to_node)},
                           {"port", Json::text(edge.edge.to_port)}})},
      {"data_plane", Json::text(edge.data_plane)},
      {"transport", transport_json(edge.transport)},
  });
}

Json route_json(const RouteDefinition& route) {
  return Json::object({
      {"destination", Json::text(route.destination)},
      {"via", route.via.empty() ? Json::null() : Json::text(route.via)},
      {"device", route.device.empty() ? Json::null() : Json::text(route.device)},
      {"install_on_create", Json::boolean(route.install_on_create)},
  });
}

Json semantics_json(NetworkProfile profile) {
  const auto& semantics = profile_semantics(profile);
  return Json::object({
      {"mac_identity", Json::text(semantics.mac_identity)},
      {"learning", Json::text(semantics.learning)},
      {"filtering", Json::text(semantics.filtering)},
      {"arp", Json::text(semantics.arp)},
      {"broadcast", Json::text(semantics.broadcast)},
      {"multicast", Json::text(semantics.multicast)},
      {"routing", Json::text(semantics.routing)},
      {"isolation", Json::text(semantics.isolation)},
      {"management", Json::text(semantics.management)},
  });
}

Json network_json(const NetworkDefinition& network) {
  const auto profile = *network.profile;
  return Json::object({
      {"id", Json::text(network.id)},
      {"profile", Json::text(to_string(profile))},
      {"semantics", semantics_json(profile)},
      {"subnets", string_array(network.subnets)},
      {"gateway", network.gateway.empty() ? Json::null() : Json::text(network.gateway)},
      {"uplink", network.uplink.empty() ? Json::null() : Json::text(network.uplink)},
      {"external", Json::boolean(network.external)},
  });
}

Json vlan_json(const VlanMetadata& vlan) {
  return Json::object({
      {"access_tag", vlan.access_tag ? Json::number(*vlan.access_tag) : Json::null()},
      {"trunks",
       converted_array(vlan.trunks, [](std::uint16_t value) { return Json::number(value); })},
  });
}

Json switch_port_json(const SwitchPortDefinition& port) {
  return Json::object({
      {"id", Json::text(port.id)},
      {"interface", Json::text(port.interface)},
      {"peer", port.peer.empty() ? Json::null() : Json::text(port.peer)},
      {"vlan", vlan_json(port.vlan)},
  });
}

Json switch_json(const SwitchDefinition& network_switch) {
  const auto mirror = network_switch.mirror;
  return Json::object({
      {"id", Json::text(network_switch.id)},
      {"kind", Json::text(to_string(network_switch.kind))},
      {"datapath", Json::text(network_switch.datapath)},
      {"ports", converted_array(network_switch.ports, switch_port_json)},
      {"mirror", mirror ? Json::object({
                              {"id", Json::text(mirror->id)},
                              {"output_port", Json::text(mirror->output_port)},
                              {"select_all", Json::boolean(mirror->select_all)},
                          })
                        : Json::null()},
  });
}

Json router_interface_json(const RouterInterfaceDefinition& interface) {
  return Json::object({
      {"id", Json::text(interface.id)},
      {"network", Json::text(interface.network)},
      {"address", Json::text(interface.address)},
      {"device", Json::text(interface.device)},
      {"peer", Json::text(interface.peer)},
      {"switch", Json::text(interface.network_switch)},
  });
}

Json policy_json(const PolicyDefinition& policy) {
  return Json::object({
      {"id", Json::text(policy.id)},
      {"source", Json::text(policy.source)},
      {"destination", Json::text(policy.destination)},
      {"action", Json::text(policy.action)},
  });
}

Json router_json(const RouterDefinition& router) {
  return Json::object({
      {"id", Json::text(router.id)},
      {"kind", Json::text(to_string(router.kind))},
      {"namespace",
       router.namespace_name.empty() ? Json::null() : Json::text(router.namespace_name)},
      {"forwarding", Json::boolean(router.forwarding)},
      {"interfaces", converted_array(router.interfaces, router_interface_json)},
      {"routes", converted_array(router.routes, route_json)},
      {"policies", converted_array(router.policies, policy_json)},
  });
}

Json attachment_json(const AttachmentDefinition& attachment) {
  return Json::object({
      {"id", Json::text(attachment.id)},
      {"kind", Json::text(to_string(attachment.kind))},
      {"owner", Json::text(attachment.owner)},
      {"network", attachment.network.empty() ? Json::null() : Json::text(attachment.network)},
      {"address", attachment.address.empty() ? Json::null() : Json::text(attachment.address)},
      {"mac", attachment.mac.empty() ? Json::null() : Json::text(attachment.mac)},
      {"interface", attachment.interface.empty() ? Json::null() : Json::text(attachment.interface)},
      {"peer", attachment.peer.empty() ? Json::null() : Json::text(attachment.peer)},
      {"switch",
       attachment.network_switch.empty() ? Json::null() : Json::text(attachment.network_switch)},
      {"mtu", Json::number(attachment.mtu)},
      {"tap_uid", Json::number(attachment.tap_uid)},
      {"tap_gid", Json::number(attachment.tap_gid)},
      {"routes", converted_array(attachment.routes, route_json)},
  });
}

Json edge_path_json(const EdgeNetworkPath& path) {
  return Json::object({
      {"edge_id", Json::text(path.edge_id)},
      {"hops", string_array(path.hops)},
  });
}

Json network_capture_json(const NetworkCaptureDefinition& capture) {
  return Json::object({
      {"id", Json::text(capture.id)},
      {"attachment", Json::text(capture.attachment)},
      {"directory", Json::text(capture.directory)},
      {"snaplen", Json::number(capture.snaplen)},
      {"max_file_bytes", Json::number(capture.max_file_bytes)},
      {"max_files", Json::number(capture.max_files)},
      {"rotation_seconds", Json::number(capture.rotation_seconds)},
      {"retention_seconds", Json::number(capture.retention_seconds)},
  });
}

Json network_fault_json(const NetworkFaultDefinition& fault) {
  return Json::object({
      {"id", Json::text(fault.id)},
      {"attachment", Json::text(fault.attachment)},
      {"delay_ms", Json::number(fault.delay_ms)},
      {"jitter_ms", Json::number(fault.jitter_ms)},
      {"loss_percent", Json::real(fault.loss_percent)},
      {"rate_kbit", Json::number(fault.rate_kbit)},
      {"duration_seconds", Json::number(fault.duration_seconds)},
  });
}

Json deployment_service_json(const DeploymentService& service) {
  return Json::object({
      {"node_id", Json::text(service.node_id)},
      {"image", Json::text(service.image)},
      {"command", Json::text(service.command)},
  });
}

Json signal_json(const ObservabilitySignalConfig& signal) {
  return Json::object({
      {"enabled", Json::boolean(signal.enabled)},
      {"exporters", string_array(signal.exporters)},
  });
}

Json deployment_json(const GraphConfig& config) {
  auto result = Json::object({
      {"project",
       config.deployment.project.empty() ? Json::null() : Json::text(config.deployment.project)},
      {"services", converted_array(config.deployment.services, deployment_service_json)},
      {"telemetry", Json::object({
                        {"service", config.deployment.telemetry_service.empty()
                                        ? Json::null()
                                        : Json::text(config.deployment.telemetry_service)},
                        {"port", Json::number(config.deployment.telemetry_port)},
                    })},
  });
  if (!config.deployment.instance_id.empty()) {
    auto instance = Json::text(config.deployment.instance_id);
    instance.member_name = "instance_id";
    result.object_value.push_back(std::move(instance));
  }
  return result;
}

Json document_json(const GraphConfig& config) {
  const auto& network = config.network_infrastructure;
  const auto& observability = config.observability;
  return Json::object({
      {"contract_version", Json::number(kNormalizedConfigContractVersion)},
      {"graph", Json::object({
                    {"id", Json::text(config.id)},
                    {"nodes", converted_array(config.nodes, node_json)},
                    {"edges", converted_array(config.edges, edge_json)},
                })},
      {"network", Json::object({
                      {"backend", Json::text("ovs")},
                      {"networks", converted_array(network.networks, network_json)},
                      {"switches", converted_array(network.switches, switch_json)},
                      {"routers", converted_array(network.routers, router_json)},
                      {"attachments", converted_array(network.attachments, attachment_json)},
                      {"edge_paths", converted_array(network.edge_paths, edge_path_json)},
                      {"captures", converted_array(network.captures, network_capture_json)},
                      {"faults", converted_array(network.faults, network_fault_json)},
                  })},
      {"deployment", deployment_json(config)},
      {"observability",
       Json::object({
           {"metrics", signal_json(observability.metrics)},
           {"tracing", signal_json(observability.tracing)},
           {"telemetry", Json::object({
                             {"host", Json::text(observability.telemetry.host)},
                             {"port", Json::number(observability.telemetry.port)},
                             {"websocket", Json::text(observability.telemetry.websocket)},
                             {"heartbeat_interval_ms",
                              Json::number(observability.telemetry.heartbeat_interval_ms)},
                             {"heartbeat_timeout_ms",
                              Json::number(observability.telemetry.heartbeat_timeout_ms)},
                         })},
           {"capture", Json::object({
                           {"enabled", Json::boolean(observability.capture.enabled)},
                           {"provider", observability.capture.provider.empty()
                                            ? Json::null()
                                            : Json::text(observability.capture.provider)},
                           {"directory", Json::text(observability.capture.directory)},
                           {"snaplen", Json::number(observability.capture.snaplen)},
                           {"max_file_bytes", Json::number(observability.capture.max_file_bytes)},
                           {"max_packets", Json::number(observability.capture.max_packets)},
                       })},
           {"otlp",
            Json::object({
                {"enabled", Json::boolean(observability.otlp.enabled)},
                {"endpoint", Json::text(observability.otlp.endpoint)},
                {"traces_path", Json::text(observability.otlp.traces_path)},
                {"metrics_path", Json::text(observability.otlp.metrics_path)},
                {"export_interval_ms", Json::number(observability.otlp.export_interval_ms)},
                {"timeout_ms", Json::number(observability.otlp.timeout_ms)},
                {"queue_capacity", Json::number(observability.otlp.queue_capacity)},
                {"max_queue_bytes", Json::number(observability.otlp.max_queue_bytes)},
                {"max_response_bytes", Json::number(observability.otlp.max_response_bytes)},
                {"retry_max_attempts", Json::number(observability.otlp.retry_max_attempts)},
                {"retry_initial_backoff_ms",
                 Json::number(observability.otlp.retry_initial_backoff_ms)},
                {"retry_max_backoff_ms", Json::number(observability.otlp.retry_max_backoff_ms)},
            })},
           {"slos",
            Json::object({
                {"window_seconds", Json::number(observability.slos.window_seconds)},
                {"minimum_window_seconds", Json::number(observability.slos.minimum_window_seconds)},
                {"availability_target", Json::real(observability.slos.availability_target)},
                {"max_error_ratio", Json::real(observability.slos.max_error_ratio)},
                {"max_drop_ratio", Json::real(observability.slos.max_drop_ratio)},
                {"max_p95_latency_us", Json::number(observability.slos.max_p95_latency_us)},
            })},
           {"history",
            Json::object({
                {"enabled", Json::boolean(observability.history.enabled)},
                {"backend", Json::text(observability.history.backend)},
                {"database_file", Json::text(observability.history.database_file)},
                {"retention_seconds", Json::number(observability.history.retention_seconds)},
                {"max_records", Json::number(observability.history.max_records)},
                {"max_database_bytes", Json::number(observability.history.max_database_bytes)},
                {"queue_capacity", Json::number(observability.history.queue_capacity)},
                {"max_queue_bytes", Json::number(observability.history.max_queue_bytes)},
                {"batch_size", Json::number(observability.history.batch_size)},
                {"flush_interval_ms", Json::number(observability.history.flush_interval_ms)},
                {"query_limit", Json::number(observability.history.query_limit)},
                {"query_timeout_ms", Json::number(observability.history.query_timeout_ms)},
                {"max_pending_queries", Json::number(observability.history.max_pending_queries)},
                {"shutdown_timeout_ms", Json::number(observability.history.shutdown_timeout_ms)},
            })},
           {"control",
            Json::object({
                {"command_timeout_ms", Json::number(observability.control.command_timeout_ms)},
                {"command_retention_seconds",
                 Json::number(observability.control.command_retention_seconds)},
                {"max_commands", Json::number(observability.control.max_commands)},
                {"max_audit_records", Json::number(observability.control.max_audit_records)},
                {"idempotency_ttl_seconds",
                 Json::number(observability.control.idempotency_ttl_seconds)},
                {"max_request_bytes", Json::number(observability.control.max_request_bytes)},
            })},
       })},
  });
}

}  // namespace

std::string normalize_config_json(const GraphConfig& config) {
  std::string output;
  render(document_json(config), output, 0);
  output.push_back('\n');
  return output;
}

}  // namespace graphx
