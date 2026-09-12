#include "config_internal.hpp"
#include "graphx/framing.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace graphx::config_internal {

void ConfigParser::parse_transports(const YAML::Node& transports, GraphConfig& config) {
  if (!require_map(transports, "transport")) return;
  strict_keys(transports, "transport", {"tcp", "udp", "unix", "in_process", "shared_memory"});
  const std::string_view sections[] = {"tcp", "udp", "unix", "in_process", "shared_memory"};
  std::unordered_set<std::string> consumed;
  for (auto& edge : config.edges) {
    const auto name = std::string(to_string(edge.transport.kind));
    const auto section = transports[name];
    const auto settings = section ? section[edge.edge.id] : YAML::Node{};
    const auto path = "transport." + name + "." + edge.edge.id;
    if (!require_map(settings, path)) continue;
    consumed.insert(name + "." + edge.edge.id);
    if (edge.transport.kind == TransportKind::tcp) {
      strict_keys(settings, path,
                  {"host", "bind", "port", "framing", "connect_timeout_ms", "send_timeout_ms",
                   "reconnect", "retry", "tls"});
      edge.transport.host = text(settings["host"], path + ".host", 253);
      edge.transport.bind = text(settings["bind"], path + ".bind", 253);
      const auto port = unsigned_value(settings["port"], path + ".port");
      if (port == 0 || port > 65535)
        error(path + ".port", "must be between 1 and 65535");
      else
        edge.transport.port = static_cast<std::uint16_t>(port);
      if (settings["framing"])
        edge.transport.framing = text(settings["framing"], path + ".framing", 16);
      if (settings["connect_timeout_ms"]) {
        edge.transport.connect_timeout_ms =
            unsigned_value(settings["connect_timeout_ms"], path + ".connect_timeout_ms");
        if (edge.transport.connect_timeout_ms == 0 || edge.transport.connect_timeout_ms > 600000)
          error(path + ".connect_timeout_ms", "must be between 1 and 600000");
      }
      if (settings["send_timeout_ms"]) {
        edge.transport.send_timeout_ms =
            unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
        if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
          error(path + ".send_timeout_ms", "must be between 1 and 600000");
      }
      edge.transport.reconnect = bool_value(settings["reconnect"], path + ".reconnect", true);
      if (const auto retry = settings["retry"]) {
        if (require_map(retry, path + ".retry")) {
          strict_keys(retry, path + ".retry",
                      {"max_attempts", "initial_backoff_ms", "max_backoff_ms"});
          if (retry["max_attempts"])
            edge.transport.retry_attempts =
                unsigned_value(retry["max_attempts"], path + ".retry.max_attempts");
          if (retry["initial_backoff_ms"])
            edge.transport.retry_initial_backoff_ms =
                unsigned_value(retry["initial_backoff_ms"], path + ".retry.initial_backoff_ms");
          if (retry["max_backoff_ms"])
            edge.transport.retry_max_backoff_ms =
                unsigned_value(retry["max_backoff_ms"], path + ".retry.max_backoff_ms");
          if (edge.transport.retry_attempts == 0 || edge.transport.retry_attempts > 1000)
            error(path + ".retry.max_attempts", "must be between 1 and 1000");
          if (edge.transport.retry_initial_backoff_ms > 600000)
            error(path + ".retry.initial_backoff_ms", "must not exceed 600000");
          if (edge.transport.retry_max_backoff_ms > 600000)
            error(path + ".retry.max_backoff_ms", "must not exceed 600000");
          if (edge.transport.retry_max_backoff_ms < edge.transport.retry_initial_backoff_ms)
            error(path + ".retry.max_backoff_ms",
                  "must be greater than or equal to initial_backoff_ms");
        }
      }
      if (const auto tls = settings["tls"]) {
        const auto tls_path = path + ".tls";
        if (require_map(tls, tls_path)) {
          strict_keys(tls, tls_path,
                      {"enabled", "verify_peer", "require_client_certificate", "ca_file",
                       "certificate_file", "private_key_file", "server_name"});
          edge.transport.tls_enabled = bool_value(tls["enabled"], tls_path + ".enabled", true);
          edge.transport.tls_verify_peer =
              bool_value(tls["verify_peer"], tls_path + ".verify_peer", true);
          edge.transport.tls_require_client_certificate = bool_value(
              tls["require_client_certificate"], tls_path + ".require_client_certificate", false);
          if (tls["ca_file"])
            edge.transport.tls_ca_file = text(tls["ca_file"], tls_path + ".ca_file", 4096);
          if (tls["certificate_file"])
            edge.transport.tls_certificate_file =
                text(tls["certificate_file"], tls_path + ".certificate_file", 4096);
          if (tls["private_key_file"])
            edge.transport.tls_private_key_file =
                text(tls["private_key_file"], tls_path + ".private_key_file", 4096);
          if (tls["server_name"])
            edge.transport.tls_server_name =
                text(tls["server_name"], tls_path + ".server_name", 253);
          if (edge.transport.tls_enabled && (edge.transport.tls_certificate_file.empty() !=
                                             edge.transport.tls_private_key_file.empty()))
            error(tls_path, "certificate_file and private_key_file must be provided together");
          if (edge.transport.tls_enabled && edge.transport.tls_certificate_file.empty())
            error(tls_path + ".certificate_file",
                  "certificate_file and private_key_file are required when TLS is enabled");
          if (edge.transport.tls_enabled && edge.transport.tls_require_client_certificate &&
              edge.transport.tls_ca_file.empty())
            error(tls_path + ".ca_file", "is required when client certificates are required");
        }
      }
    } else if (edge.transport.kind == TransportKind::udp) {
      strict_keys(
          settings, path,
          {"mode", "destination", "bind", "port", "interface", "ttl", "loopback", "reuse_address",
           "receive_buffer_bytes", "send_buffer_bytes", "max_datagram_bytes", "framing"});
      const auto mode = text(settings["mode"], path + ".mode", 16);
      if (mode == "unicast")
        edge.transport.udp_mode = UdpMode::unicast;
      else if (mode == "broadcast")
        edge.transport.udp_mode = UdpMode::broadcast;
      else if (mode == "multicast")
        edge.transport.udp_mode = UdpMode::multicast;
      else
        error(path + ".mode", "must be 'unicast', 'broadcast', or 'multicast'");
      edge.transport.destination = text(settings["destination"], path + ".destination", 15);
      edge.transport.bind = text(settings["bind"], path + ".bind", 15);
      const auto destination = ipv4_address(edge.transport.destination);
      const auto bind = ipv4_address(edge.transport.bind);
      if (!destination) error(path + ".destination", "must be an IPv4 address");
      if (!bind) error(path + ".bind", "must be an IPv4 address");
      if (destination) {
        const bool multicast = (*destination & 0xf0000000U) == 0xe0000000U;
        const bool limited_broadcast = *destination == 0xffffffffU;
        if (edge.transport.udp_mode == UdpMode::multicast && !multicast)
          error(path + ".destination", "multicast mode requires an address in 224.0.0.0/4");
        if (edge.transport.udp_mode == UdpMode::unicast && (multicast || limited_broadcast))
          error(path + ".destination", "unicast mode rejects multicast and broadcast addresses");
        if (edge.transport.udp_mode == UdpMode::broadcast && multicast)
          error(path + ".destination", "broadcast mode rejects multicast addresses");
      }
      const auto port = strict_unsigned_value(settings["port"], path + ".port");
      if (port == 0 || port > 65535)
        error(path + ".port", "must be between 1 and 65535");
      else
        edge.transport.port = static_cast<std::uint16_t>(port);
      if (settings["interface"]) {
        if (!settings["interface"].IsScalar())
          error(path + ".interface", "must be a scalar string");
        else if (settings["interface"].Scalar().size() > 15)
          error(path + ".interface", "exceeds maximum length 15");
        else
          edge.transport.interface = settings["interface"].Scalar();
        if (!edge.transport.interface.empty() && !ipv4_address(edge.transport.interface) &&
            !std::regex_match(edge.transport.interface, kInterfaceName))
          error(path + ".interface", "must be an IPv4 address or interface name");
      }
      if (settings["ttl"])
        edge.transport.ttl = strict_unsigned_value(settings["ttl"], path + ".ttl");
      if (edge.transport.ttl > 255) error(path + ".ttl", "must be between 0 and 255");
      edge.transport.loopback = strict_bool_value(settings["loopback"], path + ".loopback", true);
      edge.transport.reuse_address =
          strict_bool_value(settings["reuse_address"], path + ".reuse_address", false);
      if (settings["receive_buffer_bytes"])
        edge.transport.receive_buffer_bytes =
            strict_unsigned_value(settings["receive_buffer_bytes"], path + ".receive_buffer_bytes");
      if (edge.transport.receive_buffer_bytes < 4096 ||
          edge.transport.receive_buffer_bytes > 256U * 1024 * 1024)
        error(path + ".receive_buffer_bytes", "must be between 4096 and 268435456");
      if (settings["send_buffer_bytes"])
        edge.transport.send_buffer_bytes =
            strict_unsigned_value(settings["send_buffer_bytes"], path + ".send_buffer_bytes");
      if (edge.transport.send_buffer_bytes < 4096 ||
          edge.transport.send_buffer_bytes > 256U * 1024 * 1024)
        error(path + ".send_buffer_bytes", "must be between 4096 and 268435456");
      if (settings["max_datagram_bytes"])
        edge.transport.max_datagram_bytes =
            strict_unsigned_value(settings["max_datagram_bytes"], path + ".max_datagram_bytes");
      if (edge.transport.max_datagram_bytes < 64 || edge.transport.max_datagram_bytes > 65507)
        error(path + ".max_datagram_bytes", "must be between 64 and 65507");
      if (settings["framing"])
        edge.transport.framing = text(settings["framing"], path + ".framing", 16);
    } else if (edge.transport.kind == TransportKind::unix_socket) {
      strict_keys(settings, path, {"path", "framing", "connect_timeout_ms", "send_timeout_ms"});
      edge.transport.path = text(settings["path"], path + ".path", 103);
      if (settings["framing"])
        edge.transport.framing = text(settings["framing"], path + ".framing", 16);
      if (settings["connect_timeout_ms"])
        edge.transport.connect_timeout_ms =
            unsigned_value(settings["connect_timeout_ms"], path + ".connect_timeout_ms");
      if (edge.transport.connect_timeout_ms == 0 || edge.transport.connect_timeout_ms > 600000)
        error(path + ".connect_timeout_ms", "must be between 1 and 600000");
      if (settings["send_timeout_ms"])
        edge.transport.send_timeout_ms =
            unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
      if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
        error(path + ".send_timeout_ms", "must be between 1 and 600000");
    } else if (edge.transport.kind == TransportKind::in_process) {
      strict_keys(settings, path, {"channel", "capacity", "backpressure", "send_timeout_ms"});
      edge.transport.channel = text(settings["channel"], path + ".channel", 64);
      identifier(edge.transport.channel, path + ".channel");
      if (settings["capacity"])
        edge.transport.capacity = unsigned_value(settings["capacity"], path + ".capacity");
      if (edge.transport.capacity == 0 || edge.transport.capacity > 65536)
        error(path + ".capacity", "must be between 1 and 65536");
      if (settings["backpressure"])
        edge.transport.backpressure = text(settings["backpressure"], path + ".backpressure", 16);
      if (edge.transport.backpressure != "block" && edge.transport.backpressure != "reject")
        error(path + ".backpressure", "must be 'block' or 'reject'");
      if (settings["send_timeout_ms"])
        edge.transport.send_timeout_ms =
            unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
      if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
        error(path + ".send_timeout_ms", "must be between 1 and 600000");
    } else {
      strict_keys(settings, path,
                  {"segment", "capacity", "max_message_bytes", "backpressure", "connect_timeout_ms",
                   "send_timeout_ms"});
      edge.transport.segment = text(settings["segment"], path + ".segment", 200);
      auto segment_id = edge.transport.segment;
      if (!segment_id.empty() && segment_id.front() == '/') segment_id.erase(0, 1);
      identifier(segment_id, path + ".segment");
      if (settings["capacity"])
        edge.transport.capacity = unsigned_value(settings["capacity"], path + ".capacity");
      if (edge.transport.capacity == 0 || edge.transport.capacity > 65536)
        error(path + ".capacity", "must be between 1 and 65536");
      if (settings["max_message_bytes"])
        edge.transport.max_message_bytes =
            unsigned_value(settings["max_message_bytes"], path + ".max_message_bytes");
      if (edge.transport.max_message_bytes < 64 ||
          edge.transport.max_message_bytes > kMaxFrameBytes + 4)
        error(path + ".max_message_bytes", "must be between 64 and 16777220");
      if (static_cast<std::uint64_t>(edge.transport.capacity) *
                  (edge.transport.max_message_bytes + 32ULL) +
              4096ULL >
          256ULL * 1024 * 1024)
        error(path, "shared-memory payload capacity must not exceed 256 MiB");
      if (settings["backpressure"])
        edge.transport.backpressure = text(settings["backpressure"], path + ".backpressure", 16);
      if (edge.transport.backpressure != "block" && edge.transport.backpressure != "reject")
        error(path + ".backpressure", "must be 'block' or 'reject'");
      if (settings["send_timeout_ms"])
        edge.transport.send_timeout_ms =
            unsigned_value(settings["send_timeout_ms"], path + ".send_timeout_ms");
      if (edge.transport.send_timeout_ms == 0 || edge.transport.send_timeout_ms > 600000)
        error(path + ".send_timeout_ms", "must be between 1 and 600000");
      if (settings["connect_timeout_ms"])
        edge.transport.connect_timeout_ms =
            unsigned_value(settings["connect_timeout_ms"], path + ".connect_timeout_ms");
      if (edge.transport.connect_timeout_ms == 0 || edge.transport.connect_timeout_ms > 600000)
        error(path + ".connect_timeout_ms", "must be between 1 and 600000");
    }
    if (edge.data_plane == "external") {
      if (edge.transport.kind != TransportKind::tcp && edge.transport.kind != TransportKind::udp)
        error(path, "external data-plane edges support only TCP or UDP");
      if (edge.transport.framing != "none")
        error(path + ".framing", "external data-plane edges require 'none'");
    } else if (edge.transport.kind != TransportKind::in_process &&
               edge.transport.framing != "u32be") {
      error(path + ".framing", "GraphX data-plane edges require 'u32be'");
    }
  }
  for (const auto section_name : sections) {
    const auto section = transports[std::string(section_name)];
    if (!section) continue;
    const auto path = "transport." + std::string(section_name);
    if (!require_map(section, path)) continue;
    std::unordered_set<std::string> seen;
    for (const auto& entry : section) {
      if (!entry.first.IsScalar()) {
        error(path, "contains a non-scalar edge id");
        continue;
      }
      const auto id = entry.first.Scalar();
      if (!seen.insert(id).second) error(path + "." + id, "duplicate transport entry");
      if (!consumed.contains(std::string(section_name) + "." + id))
        error(path + "." + id, "does not correspond to an edge using this transport");
    }
  }
}

}  // namespace graphx::config_internal
