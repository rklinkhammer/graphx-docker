#include "config_document.hpp"

namespace graphx::config_internal {
TransportSettings resolved_transport(const ConfigValue& connection) {
  const bool external = connection.at("encoding") == Value("raw");
  TransportSettings result;
  const auto& settings = connection.at("settings");
  const auto transport = connection.at("transport").text();
  if (transport == "tcp") {
    TcpTransportConfig tcp;
    tcp.host = settings.at("host").text();
    tcp.bind = settings.at("bind").text();
    tcp.port = static_cast<std::uint16_t>(settings.at("port").integer());
    tcp.framing = settings.at("framing").text();
    tcp.tls.enabled = connection.at("security").at("profile") != Value("none");
    tcp.tls.require_client_certificate = tcp.tls.enabled;
    tcp.connect_timeout_ms =
        static_cast<std::uint32_t>(settings.at("connect_timeout_ms").integer());
    tcp.send_timeout_ms = static_cast<std::uint32_t>(settings.at("send_timeout_ms").integer());
    tcp.reconnect = settings.at("reconnect").boolean();
    tcp.retry = {
        static_cast<std::uint32_t>(settings.at("retry").at("max_attempts").integer()),
        static_cast<std::uint32_t>(settings.at("retry").at("initial_backoff_ms").integer()),
        static_cast<std::uint32_t>(settings.at("retry").at("max_backoff_ms").integer())};
    result = external ? TransportSettings(ExternalTransportConfig{tcp}) : TransportSettings(tcp);
  } else if (transport == "udp") {
    UdpTransportConfig udp;
    udp.destination = settings.at("destination").text();
    udp.bind = settings.at("bind").text();
    udp.port = static_cast<std::uint16_t>(settings.at("port").integer());
    const auto mode = settings.at("mode").text();
    udp.mode = mode == "multicast"   ? UdpMode::multicast
               : mode == "broadcast" ? UdpMode::broadcast
                                     : UdpMode::unicast;
    udp.interface = string_or(settings, "interface");
    udp.ttl = static_cast<std::uint32_t>(settings.at("ttl").integer());
    udp.loopback = settings.at("loopback").boolean();
    udp.reuse_address = settings.at("reuse_address").boolean();
    udp.max_datagram_bytes =
        static_cast<std::uint32_t>(settings.at("max_datagram_bytes").integer());
    udp.receive_buffer_bytes =
        static_cast<std::uint32_t>(settings.at("receive_buffer_bytes").integer());
    udp.send_buffer_bytes = static_cast<std::uint32_t>(settings.at("send_buffer_bytes").integer());
    udp.framing = settings.at("framing").text();
    result = external ? TransportSettings(ExternalTransportConfig{udp}) : TransportSettings(udp);
  } else {
    SharedMemoryTransportConfig shared;
    shared.segment = settings.at("segment").text();
    shared.capacity = static_cast<std::uint32_t>(settings.at("capacity").integer());
    shared.max_message_bytes =
        static_cast<std::uint32_t>(settings.at("max_message_bytes").integer());
    shared.backpressure = settings.at("backpressure").text();
    shared.connect_timeout_ms =
        static_cast<std::uint32_t>(settings.at("connect_timeout_ms").integer());
    shared.send_timeout_ms = static_cast<std::uint32_t>(settings.at("send_timeout_ms").integer());
    result = shared;
  }
  return result;
}
}  // namespace graphx::config_internal
