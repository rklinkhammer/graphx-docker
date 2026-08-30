#include "graphx/transport_factory.hpp"

#include "graphx/tcp_transport.hpp"
#include "graphx/shared_memory_transport.hpp"
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
      options.retry.initial_backoff =
          std::chrono::milliseconds(transport.retry_initial_backoff_ms);
      options.retry.max_backoff = std::chrono::milliseconds(transport.retry_max_backoff_ms);
      options.reconnect = transport.reconnect;
      if (mode == ConnectionMode::connect)
        return std::make_unique<TcpTransport>(
            TcpTransport::connect(endpoint, edge.edge.id, trace_sink, options));
      return std::make_unique<TcpTransport>(
          TcpTransport::listen(endpoint, edge.edge.id, trace_sink, options));
    }
    case TransportKind::unix_socket:
      if (transport.path.empty())
        throw std::invalid_argument("Unix-domain transport requires a path");
      if (mode == ConnectionMode::connect)
        return std::make_unique<UnixDomainSocketTransport>(
            UnixDomainSocketTransport::connect(transport.path, edge.edge.id, trace_sink));
      return std::make_unique<UnixDomainSocketTransport>(
          UnixDomainSocketTransport::listen(transport.path, edge.edge.id, trace_sink));
    case TransportKind::shared_memory: {
      if (transport.segment.empty())
        throw std::invalid_argument("shared-memory transport requires a segment name");
      SharedMemoryOptions options;
      options.capacity = transport.capacity;
      options.max_message_bytes = transport.max_message_bytes;
      options.send_timeout = std::chrono::milliseconds(transport.send_timeout_ms);
      options.connect_timeout = std::chrono::milliseconds(transport.connect_timeout_ms);
      options.backpressure = transport.backpressure == "reject"
                                 ? SharedMemoryBackpressure::reject
                                 : SharedMemoryBackpressure::block;
      if (mode == ConnectionMode::connect)
        return std::make_unique<SharedMemoryTransport>(SharedMemoryTransport::connect(
            transport.segment, edge.edge.id, trace_sink, options));
      return std::make_unique<SharedMemoryTransport>(SharedMemoryTransport::listen(
          transport.segment, edge.edge.id, trace_sink, options));
    }
    case TransportKind::in_process: {
      if (transport.channel.empty())
        throw std::invalid_argument("in-process transport requires a channel name");
      std::shared_ptr<InProcessChannel> channel;
      {
        std::scoped_lock lock(mutex_);
        channel = channels_[transport.channel].lock();
        if (!channel) {
          channel = std::make_shared<InProcessChannel>();
          channels_[transport.channel] = channel;
        }
      }
      return std::make_unique<InProcessTransport>(std::move(channel), edge.edge.id, trace_sink);
    }
  }
  throw std::invalid_argument("unsupported transport kind");
}

}  // namespace graphx
