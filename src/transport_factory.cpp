#include "graphx/transport_factory.hpp"

#include "graphx/tcp_transport.hpp"
#include "graphx/shared_memory_transport.hpp"
#include "graphx/udp_transport.hpp"
#include "graphx/unix_domain_socket_transport.hpp"

#include <stdexcept>

namespace graphx {

TransportPtr TransportFactory::create(const EdgeConfig& edge, ConnectionMode mode,
                                      TraceSink* trace_sink) {
  const auto& transport = edge.transport;
  switch (transport.kind) {
    case TransportKind::tcp: {
      if (transport.port == 0 || transport.host.empty() || transport.bind.empty())
        throw std::invalid_argument("TCP transport requires host, bind, and nonzero port");
      const Endpoint endpoint{mode == ConnectionMode::connect ? transport.host : transport.bind,
                              transport.port};
      TcpOptions options;
      options.connect_timeout = std::chrono::milliseconds(transport.connect_timeout_ms);
      options.send_timeout = std::chrono::milliseconds(transport.send_timeout_ms);
      options.retry.max_attempts = transport.retry_attempts;
      options.retry.initial_backoff = std::chrono::milliseconds(transport.retry_initial_backoff_ms);
      options.retry.max_backoff = std::chrono::milliseconds(transport.retry_max_backoff_ms);
      options.reconnect = transport.reconnect;
      options.tls.enabled = transport.tls_enabled;
      options.tls.verify_peer = transport.tls_verify_peer;
      options.tls.require_client_certificate = transport.tls_require_client_certificate;
      options.tls.ca_file = transport.tls_ca_file;
      options.tls.certificate_file = transport.tls_certificate_file;
      options.tls.private_key_file = transport.tls_private_key_file;
      options.tls.server_name = transport.tls_server_name;
      if (mode == ConnectionMode::connect)
        return std::make_unique<TcpTransport>(
            TcpTransport::connect(endpoint, edge.edge.id, trace_sink, options));
      return std::make_unique<TcpTransport>(
          TcpTransport::listen(endpoint, edge.edge.id, trace_sink, options));
    }
    case TransportKind::udp: {
      if (transport.port == 0 || transport.destination.empty() || transport.bind.empty())
        throw std::invalid_argument("UDP transport requires destination, bind, and nonzero port");
      UdpOptions options;
      options.mode = transport.udp_mode;
      options.interface = transport.interface;
      options.ttl = static_cast<std::uint8_t>(transport.ttl);
      options.loopback = transport.loopback;
      options.reuse_address = transport.reuse_address;
      options.receive_buffer_bytes = transport.receive_buffer_bytes;
      options.send_buffer_bytes = transport.send_buffer_bytes;
      options.max_datagram_bytes = transport.max_datagram_bytes;
      if (mode == ConnectionMode::connect)
        return std::make_unique<UdpTransport>(
            UdpTransport::connect({transport.destination, transport.port}, transport.bind,
                                  edge.edge.id, trace_sink, options));
      return std::make_unique<UdpTransport>(
          UdpTransport::listen({transport.bind, transport.port}, transport.destination,
                               edge.edge.id, trace_sink, options));
    }
    case TransportKind::unix_socket:
      if (transport.path.empty())
        throw std::invalid_argument("Unix-domain transport requires a path");
      {
        UnixDomainSocketOptions options;
        options.connect_timeout = std::chrono::milliseconds(transport.connect_timeout_ms);
        options.send_timeout = std::chrono::milliseconds(transport.send_timeout_ms);
        if (mode == ConnectionMode::connect)
          return std::make_unique<UnixDomainSocketTransport>(UnixDomainSocketTransport::connect(
              transport.path, edge.edge.id, trace_sink, options));
        return std::make_unique<UnixDomainSocketTransport>(
            UnixDomainSocketTransport::listen(transport.path, edge.edge.id, trace_sink, options));
      }
    case TransportKind::shared_memory: {
      if (transport.segment.empty())
        throw std::invalid_argument("shared-memory transport requires a segment name");
      SharedMemoryOptions options;
      options.capacity = transport.capacity;
      options.max_message_bytes = transport.max_message_bytes;
      options.send_timeout = std::chrono::milliseconds(transport.send_timeout_ms);
      options.connect_timeout = std::chrono::milliseconds(transport.connect_timeout_ms);
      options.backpressure = transport.backpressure == "reject" ? SharedMemoryBackpressure::reject
                                                                : SharedMemoryBackpressure::block;
      if (mode == ConnectionMode::connect)
        return std::make_unique<SharedMemoryTransport>(
            SharedMemoryTransport::connect(transport.segment, edge.edge.id, trace_sink, options));
      return std::make_unique<SharedMemoryTransport>(
          SharedMemoryTransport::listen(transport.segment, edge.edge.id, trace_sink, options));
    }
    case TransportKind::in_process: {
      if (transport.channel.empty())
        throw std::invalid_argument("in-process transport requires a channel name");
      InProcessOptions options;
      options.capacity = transport.capacity;
      options.send_timeout = std::chrono::milliseconds(transport.send_timeout_ms);
      options.backpressure = transport.backpressure == "reject" ? InProcessBackpressure::reject
                                                                : InProcessBackpressure::block;
      std::shared_ptr<InProcessChannel> channel;
      {
        std::scoped_lock lock(mutex_);
        channel = channels_[transport.channel].lock();
        if (!channel) {
          channel = std::make_shared<InProcessChannel>(options);
          channels_[transport.channel] = channel;
        } else if (channel->options().capacity != options.capacity ||
                   channel->options().backpressure != options.backpressure ||
                   channel->options().send_timeout != options.send_timeout) {
          throw std::invalid_argument("in-process channel '" + transport.channel +
                                      "' has inconsistent queue settings");
        }
      }
      return std::make_unique<InProcessTransport>(std::move(channel), edge.edge.id, trace_sink);
    }
  }
  throw std::invalid_argument("unsupported transport kind");
}

}  // namespace graphx
