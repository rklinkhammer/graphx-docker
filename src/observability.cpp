#include "graphx/observability.hpp"

#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <condition_variable>
#include <cerrno>
#include <deque>
#include <iomanip>
#include <sstream>
#include <thread>

namespace graphx {
namespace {

constexpr std::array<std::chrono::microseconds, 7> kLatencyBounds{
    std::chrono::microseconds{10}, std::chrono::microseconds{50},
    std::chrono::microseconds{100}, std::chrono::microseconds{500},
    std::chrono::microseconds{1000}, std::chrono::microseconds{5000},
    std::chrono::microseconds{10000}};

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

std::string hex_id(std::string_view value, std::size_t digits, std::uint64_t salt = 0) {
  const auto hash = [](std::string_view text, std::uint64_t seed) {
    auto result = seed;
    for (const auto byte : text) {
      result ^= static_cast<unsigned char>(byte);
      result *= 1099511628211ULL;
    }
    return result;
  };
  const auto first = hash(value, 14695981039346656037ULL) ^ salt;
  const auto second = hash(value, 1099511628211ULL ^ salt);
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << first;
  if (digits > 16) out << std::setw(16) << second;
  return out.str().substr(0, digits);
}

void post_json(std::string_view host, std::uint16_t port, std::string_view path,
               std::string_view body) noexcept {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses{};
  const auto service = std::to_string(port);
  if (::getaddrinfo(std::string(host).c_str(), service.c_str(), &hints, &addresses) != 0) return;
  int socket = -1;
  for (auto* address = addresses; address; address = address->ai_next) {
    socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket < 0) continue;
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
    timeval timeout{0, 500000};
    ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags >= 0) ::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
    int connected = ::connect(socket, address->ai_addr, address->ai_addrlen);
    if (connected != 0 && errno == EINPROGRESS) {
      pollfd descriptor{socket, POLLOUT, 0};
      if (::poll(&descriptor, 1, 500) > 0) {
        int error{};
        socklen_t size = sizeof(error);
        if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0)
          connected = 0;
      }
    }
    if (connected == 0) {
      if (flags >= 0) ::fcntl(socket, F_SETFL, flags);
      break;
    }
    ::close(socket);
    socket = -1;
  }
  ::freeaddrinfo(addresses);
  if (socket < 0) return;
  std::ostringstream request;
  request << "POST " << path << " HTTP/1.1\r\nHost: " << host << ':' << port
          << "\r\nContent-Type: application/json\r\nContent-Length: " << body.size()
          << "\r\nConnection: close\r\n\r\n" << body;
  const auto data = request.str();
  std::size_t offset{};
  while (offset < data.size()) {
    const auto sent = ::send(socket, data.data() + offset, data.size() - offset,
#ifdef MSG_NOSIGNAL
                             MSG_NOSIGNAL
#else
                             0
#endif
    );
    if (sent <= 0) break;
    offset += static_cast<std::size_t>(sent);
  }
  ::close(socket);
}

}  // namespace

std::string_view to_string(ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::disconnected: return "disconnected";
    case ConnectionState::connecting: return "connecting";
    case ConnectionState::listening: return "listening";
    case ConnectionState::connected: return "connected";
    case ConnectionState::closed: return "closed";
    case ConnectionState::error: return "error";
  }
  return "unknown";
}

void MetricsTraceSink::on_send(std::string_view edge_id, const Envelope&,
                               std::size_t wire_bytes) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.sent;
  value.sent_wire_bytes += wire_bytes;
}

void MetricsTraceSink::on_receive(std::string_view edge_id, const Envelope&,
                                  std::size_t wire_bytes,
                                  std::chrono::nanoseconds latency) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.received;
  value.received_wire_bytes += wire_bytes;
  value.total_latency += latency;
  std::size_t bucket{};
  while (bucket < kLatencyBounds.size() && latency > kLatencyBounds[bucket]) ++bucket;
  ++value.latency_buckets[bucket];
}

void MetricsTraceSink::on_error(std::string_view edge_id, std::string_view) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.errors;
  value.connection = ConnectionState::error;
}

void MetricsTraceSink::on_connection(std::string_view edge_id, ConnectionState state) {
  std::scoped_lock lock(mutex_);
  edges_[std::string(edge_id)].connection = state;
}

void MetricsTraceSink::on_reconnect(std::string_view edge_id) {
  std::scoped_lock lock(mutex_);
  ++edges_[std::string(edge_id)].reconnects;
}

void MetricsTraceSink::on_backpressure(std::string_view edge_id,
                                       std::chrono::nanoseconds duration, bool rejected) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.backpressure_events;
  value.total_backpressure += duration;
  if (rejected) ++value.rejected;
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

void CompositeTraceSink::on_connection(std::string_view edge_id, ConnectionState state) {
  for (auto* sink : sinks_) sink->on_connection(edge_id, state);
}

void CompositeTraceSink::on_reconnect(std::string_view edge_id) {
  for (auto* sink : sinks_) sink->on_reconnect(edge_id);
}

void CompositeTraceSink::on_backpressure(std::string_view edge_id,
                                         std::chrono::nanoseconds duration, bool rejected) {
  for (auto* sink : sinks_) sink->on_backpressure(edge_id, duration, rejected);
}

void CompositeTraceSink::on_processing(std::string_view node_id, const Envelope& envelope,
                                       std::chrono::nanoseconds duration, bool success) {
  for (auto* sink : sinks_) sink->on_processing(node_id, envelope, duration, success);
}

void CompositeTraceSink::on_heartbeat(std::string_view node_id, double cpu_percent) {
  for (auto* sink : sinks_) sink->on_heartbeat(node_id, cpu_percent);
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

void UdpJsonTraceSink::on_connection(std::string_view edge_id, ConnectionState state) {
  emit("connection", edge_id, nullptr, 0, {}, to_string(state));
}

void UdpJsonTraceSink::on_reconnect(std::string_view edge_id) {
  emit("reconnect", edge_id, nullptr, 0, {});
}

void UdpJsonTraceSink::on_backpressure(std::string_view edge_id,
                                       std::chrono::nanoseconds duration, bool rejected) {
  emit("backpressure", edge_id, nullptr, 0, duration, rejected ? "rejected" : "blocked");
}

void UdpJsonTraceSink::on_processing(std::string_view node_id, const Envelope& envelope,
                                     std::chrono::nanoseconds duration, bool success) {
  emit("processing", node_id, &envelope, 0, duration, success ? "ok" : "error");
}

void UdpJsonTraceSink::on_heartbeat(std::string_view node_id, double cpu_percent) {
  emit("heartbeat", node_id, nullptr, 0, {}, {}, cpu_percent);
}

void UdpJsonTraceSink::on_capture(std::string_view edge_id, const Envelope& envelope,
                                  std::string_view direction, std::string_view file,
                                  std::uint64_t packet_index, std::uint64_t file_offset) {
  if (!impl_ || impl_->socket < 0) return;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::ostringstream json;
  json << "{\"kind\":\"capture\",\"event\":\"frame\",\"nodeId\":\""
       << escape_json(impl_->node_id) << "\",\"edgeId\":\"" << escape_json(edge_id)
       << "\",\"timestamp\":" << now << ",\"sequence\":" << envelope.sequence
       << ",\"type\":\"" << escape_json(envelope.type) << "\",\"traceId\":\""
       << escape_json(envelope.trace_id) << "\",\"direction\":\""
       << escape_json(direction) << "\",\"captureFile\":\"" << escape_json(file)
       << "\",\"capturePacket\":" << packet_index << ",\"captureOffset\":"
       << file_offset << '}';
  const auto value = json.str();
  ::send(impl_->socket, value.data(), value.size(), 0);
}

void UdpJsonTraceSink::emit(std::string_view event, std::string_view edge_id,
                            const Envelope* envelope, std::size_t wire_bytes,
                            std::chrono::nanoseconds latency,
                            std::string_view message, double cpu_percent) {
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
  if (cpu_percent >= 0.0)
    json << ",\"cpuPercent\":" << std::fixed << std::setprecision(3) << cpu_percent;
  json << '}';
  const auto value = json.str();
  ::send(impl_->socket, value.data(), value.size(), 0);
}

struct OtlpHttpTraceSink::Impl {
  std::string node_id;
  std::string host;
  std::uint16_t port{};
  std::string path;
  std::size_t capacity{};
  std::mutex mutex;
  std::condition_variable ready;
  std::condition_variable drained;
  std::deque<std::string> queue;
  bool stopping{};
  bool active{};
  std::thread worker;

  void run() {
    for (;;) {
      std::string payload;
      {
        std::unique_lock lock(mutex);
        ready.wait(lock, [&] { return stopping || !queue.empty(); });
        if (queue.empty() && stopping) return;
        payload = std::move(queue.front());
        queue.pop_front();
        active = true;
      }
      post_json(host, port, path, payload);
      {
        std::scoped_lock lock(mutex);
        active = false;
      }
      drained.notify_all();
    }
  }
};

OtlpHttpTraceSink::OtlpHttpTraceSink(std::string node_id, std::string host,
                                     std::uint16_t port, std::string path,
                                     std::size_t queue_capacity)
    : impl_(std::make_unique<Impl>()) {
  impl_->node_id = std::move(node_id);
  impl_->host = std::move(host);
  impl_->port = port;
  impl_->path = std::move(path);
  impl_->capacity = std::max<std::size_t>(1, queue_capacity);
  impl_->worker = std::thread([this] { impl_->run(); });
}

OtlpHttpTraceSink::~OtlpHttpTraceSink() {
  if (!impl_) return;
  {
    std::unique_lock lock(impl_->mutex);
    impl_->drained.wait_for(lock, std::chrono::milliseconds(600),
                            [&] { return impl_->queue.empty() && !impl_->active; });
    impl_->stopping = true;
    impl_->queue.clear();
  }
  impl_->ready.notify_one();
  if (impl_->worker.joinable()) impl_->worker.join();
}

void OtlpHttpTraceSink::on_send(std::string_view edge_id, const Envelope& envelope,
                                std::size_t wire_bytes) {
  enqueue_span("graphx.send", edge_id, &envelope, {}, "ok", wire_bytes);
}

void OtlpHttpTraceSink::on_receive(std::string_view edge_id, const Envelope& envelope,
                                   std::size_t wire_bytes,
                                   std::chrono::nanoseconds latency) {
  enqueue_span("graphx.receive", edge_id, &envelope, latency, "ok", wire_bytes);
}

void OtlpHttpTraceSink::on_error(std::string_view edge_id, std::string_view message) {
  enqueue_span("graphx.error", edge_id, nullptr, {}, message);
}

void OtlpHttpTraceSink::on_processing(std::string_view node_id, const Envelope& envelope,
                                      std::chrono::nanoseconds duration, bool success) {
  enqueue_span("graphx.process", node_id, &envelope, duration, success ? "ok" : "error");
}

void OtlpHttpTraceSink::enqueue_span(std::string_view name, std::string_view subject,
                                     const Envelope* envelope,
                                     std::chrono::nanoseconds duration,
                                     std::string_view status, std::size_t wire_bytes) {
  if (!impl_) return;
  const auto end = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const auto start = end - std::max<std::int64_t>(0, duration.count());
  const auto trace_source = envelope ? std::string_view(envelope->trace_id) : subject;
  const auto salt = envelope ? envelope->sequence : static_cast<std::uint64_t>(end);
  std::ostringstream json;
  json << "{\"resourceSpans\":[{\"resource\":{\"attributes\":[{\"key\":\"service.name\","
          "\"value\":{\"stringValue\":\"graphx-" << escape_json(impl_->node_id)
       << "\"}}]},\"scopeSpans\":[{\"scope\":{\"name\":\"graphx.runtime\"},\"spans\":[{"
          "\"traceId\":\"" << hex_id(trace_source, 32)
       << "\",\"spanId\":\"" << hex_id(subject, 16, salt)
       << "\",\"name\":\"" << name << ' ' << escape_json(subject)
       << "\",\"startTimeUnixNano\":\"" << start << "\",\"endTimeUnixNano\":\""
       << end << "\",\"attributes\":[{\"key\":\"graphx.subject\",\"value\":{\"stringValue\":\""
       << escape_json(subject) << "\"}},{\"key\":\"graphx.status\",\"value\":{\"stringValue\":\""
       << escape_json(status) << "\"}},{\"key\":\"graphx.wire_bytes\",\"value\":{\"intValue\":\""
       << wire_bytes << "\"}}";
  if (envelope)
    json << ",{\"key\":\"graphx.sequence\",\"value\":{\"intValue\":\""
         << envelope->sequence << "\"}}";
  json << "]}]}]}]}";
  {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->stopping || impl_->queue.size() >= impl_->capacity) return;
    impl_->queue.push_back(json.str());
  }
  impl_->ready.notify_one();
}

}  // namespace graphx
