#include "graphx/observability.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sstream>

namespace graphx {
namespace {

std::string escape_json(std::string_view value) {
  std::string result;
  for (const char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += character;
    }
  }
  return result;
}

}  // namespace

void MetricsTraceSink::on_send(std::string_view edge_id, const Envelope&,
                               std::size_t wire_bytes) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.sent;
  value.wire_bytes += wire_bytes;
}

void MetricsTraceSink::on_receive(std::string_view edge_id, const Envelope&,
                                  std::size_t wire_bytes,
                                  std::chrono::nanoseconds latency) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.received;
  value.wire_bytes += wire_bytes;
  value.total_latency += latency;
}

void MetricsTraceSink::on_error(std::string_view edge_id, std::string_view) {
  std::scoped_lock lock(mutex_);
  ++edges_[std::string(edge_id)].errors;
}

EdgeMetrics MetricsTraceSink::edge(std::string_view edge_id) const {
  std::scoped_lock lock(mutex_);
  if (const auto found = edges_.find(std::string(edge_id)); found != edges_.end())
    return found->second;
  return {};
}

void CompositeTraceSink::on_send(std::string_view edge_id, const Envelope& envelope,
                                 std::size_t wire_bytes) {
  for (auto* sink : sinks_) sink->on_send(edge_id, envelope, wire_bytes);
}

void CompositeTraceSink::on_receive(std::string_view edge_id, const Envelope& envelope,
                                    std::size_t wire_bytes,
                                    std::chrono::nanoseconds latency) {
  for (auto* sink : sinks_) sink->on_receive(edge_id, envelope, wire_bytes, latency);
}

void CompositeTraceSink::on_error(std::string_view edge_id, std::string_view message) {
  for (auto* sink : sinks_) sink->on_error(edge_id, message);
}

struct UdpJsonTraceSink::Impl {
  std::string node_id;
  int socket{-1};
};

UdpJsonTraceSink::UdpJsonTraceSink(std::string node_id, std::string host,
                                   std::uint16_t port)
    : impl_(std::make_unique<Impl>()) {
  impl_->node_id = std::move(node_id);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* addresses{};
  const auto service = std::to_string(port);
  if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) return;
  for (auto* address = addresses; address; address = address->ai_next) {
    const int candidate = ::socket(address->ai_family, address->ai_socktype,
                                   address->ai_protocol);
    if (candidate >= 0 && ::connect(candidate, address->ai_addr,
                                    address->ai_addrlen) == 0) {
      impl_->socket = candidate;
      break;
    }
    if (candidate >= 0) ::close(candidate);
  }
  ::freeaddrinfo(addresses);
}

UdpJsonTraceSink::~UdpJsonTraceSink() {
  if (impl_ && impl_->socket >= 0) ::close(impl_->socket);
}

void UdpJsonTraceSink::on_send(std::string_view edge_id, const Envelope& envelope,
                               std::size_t wire_bytes) {
  emit("send", edge_id, &envelope, wire_bytes, {});
}

void UdpJsonTraceSink::on_receive(std::string_view edge_id,
                                  const Envelope& envelope,
                                  std::size_t wire_bytes,
                                  std::chrono::nanoseconds latency) {
  emit("receive", edge_id, &envelope, wire_bytes, latency);
}

void UdpJsonTraceSink::on_error(std::string_view edge_id,
                                std::string_view message) {
  emit("error", edge_id, nullptr, 0, {}, message);
}

void UdpJsonTraceSink::emit(std::string_view event, std::string_view edge_id,
                            const Envelope* envelope, std::size_t wire_bytes,
                            std::chrono::nanoseconds latency,
                            std::string_view message) {
  if (!impl_ || impl_->socket < 0) return;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::ostringstream json;
  json << "{\"kind\":\"trace\",\"event\":\"" << event
       << "\",\"nodeId\":\"" << escape_json(impl_->node_id)
       << "\",\"edgeId\":\"" << escape_json(edge_id)
       << "\",\"timestamp\":" << now << ",\"wireBytes\":" << wire_bytes
       << ",\"latencyUs\":"
       << std::chrono::duration_cast<std::chrono::microseconds>(latency).count();
  if (envelope) {
    json << ",\"sequence\":" << envelope->sequence
         << ",\"type\":\"" << escape_json(envelope->type)
         << "\",\"payloadBytes\":" << envelope->payload.size()
         << ",\"traceId\":\"" << escape_json(envelope->trace_id) << '"';
  }
  if (!message.empty()) json << ",\"message\":\"" << escape_json(message) << '"';
  json << '}';
  const auto value = json.str();
  ::send(impl_->socket, value.data(), value.size(), 0);
}

}  // namespace graphx
