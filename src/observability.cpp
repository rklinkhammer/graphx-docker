#include "graphx/observability.hpp"

#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <sys/time.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/hmac.h>

#include <condition_variable>
#include <atomic>
#include <cerrno>
#include <deque>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace graphx {
namespace {

constexpr std::array<std::chrono::microseconds, 7> kLatencyBounds{
    std::chrono::microseconds{10},   std::chrono::microseconds{50},
    std::chrono::microseconds{100},  std::chrono::microseconds{500},
    std::chrono::microseconds{1000}, std::chrono::microseconds{5000},
    std::chrono::microseconds{10000}};

std::string escape_json(std::string_view value) {
  std::string result;
  for (const char character : value) {
    switch (character) {
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
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
      default: {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte >= 0x7f) {
          constexpr std::string_view digits = "0123456789abcdef";
          result += "\\u00";
          result += digits[byte >> 4];
          result += digits[byte & 0x0f];
        } else {
          result += character;
        }
      }
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

std::string generate_span_id() {
  // Span identity must remain fresh across process boundaries, including when a
  // prefork parent has already initialized GraphX's message identity state.
  for (;;) {
    std::uint64_t value{};
    if (::getentropy(&value, sizeof(value)) != 0) {
      // getentropy is available on supported macOS/Linux systems. Keep a
      // standard-library entropy fallback so telemetry remains best effort if a
      // constrained runtime denies the system call.
      std::random_device random;
      for (int index = 0; index < 4; ++index)
        value = (value << 16) ^ (static_cast<std::uint64_t>(random()) & 0xffffU);
    }
    if (value == 0) continue;
    std::ostringstream span_id;
    span_id << std::hex << std::setfill('0') << std::setw(16) << value;
    return span_id.str();
  }
}

std::string random_hex_nonce() {
  std::array<unsigned char, 16> bytes{};
  if (::getentropy(bytes.data(), bytes.size()) != 0) throw std::runtime_error("getentropy failed");
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    result += digits[byte >> 4];
    result += digits[byte & 0x0f];
  }
  return result;
}

std::string hmac_hex(std::string_view secret, std::string_view value) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size{};
  if (!HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
            reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest.data(),
            &size))
    throw std::runtime_error("HMAC-SHA256 failed");
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (unsigned int index = 0; index < size; ++index) {
    result += digits[digest[index] >> 4];
    result += digits[digest[index] & 0x0f];
  }
  return result;
}

std::string signed_datagram(std::string_view payload, std::string_view secret) {
  if (secret.empty()) return std::string(payload);
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const auto nonce = random_hex_nonce();
  const auto signed_value = std::to_string(timestamp) + "." + nonce + "." + std::string(payload);
  return "{\"payload\":" + std::string(payload) +
         ",\"auth\":{\"timestamp\":" + std::to_string(timestamp) + ",\"nonce\":\"" + nonce +
         "\",\"signature\":\"" + hmac_hex(secret, signed_value) + "\"}}";
}

std::string_view json_auth_field(std::string_view input, std::string_view name,
                                 std::size_t length) {
  std::string marker;
  marker.reserve(name.size() + 4);
  marker += '"';
  marker += name;
  marker += "\":\"";
  const auto start = input.find(marker);
  if (start == std::string_view::npos) return {};
  const auto value = start + marker.size();
  if (value + length >= input.size() || input[value + length] != '"') return {};
  return input.substr(value, length);
}

bool authenticate_command(std::string_view input, std::string_view secret,
                          std::unordered_set<std::string>& nonces) {
  if (secret.empty()) return false;
  constexpr std::string_view payload_marker = "{\"payload\":";
  const auto auth = input.rfind(",\"auth\":{");
  if (!input.starts_with(payload_marker) || auth == std::string_view::npos ||
      !input.ends_with("}}"))
    return false;
  const auto payload = input.substr(payload_marker.size(), auth - payload_marker.size());
  const auto timestamp_marker = input.find("\"timestamp\":", auth);
  if (timestamp_marker == std::string_view::npos) return false;
  const auto timestamp_start = timestamp_marker + 12;
  const auto timestamp_end = input.find(',', timestamp_start);
  if (timestamp_end == std::string_view::npos) return false;
  std::int64_t timestamp{};
  try {
    timestamp =
        std::stoll(std::string(input.substr(timestamp_start, timestamp_end - timestamp_start)));
  } catch (...) {
    return false;
  }
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  if (timestamp < now - 30000 || timestamp > now + 30000) return false;
  const auto nonce = json_auth_field(input.substr(auth), "nonce", 32);
  const auto signature = json_auth_field(input.substr(auth), "signature", 64);
  if (nonce.empty() || signature.empty() || nonces.contains(std::string(nonce))) return false;
  const auto expected = hmac_hex(
      secret, std::to_string(timestamp) + "." + std::string(nonce) + "." + std::string(payload));
  if (signature.size() != expected.size() ||
      CRYPTO_memcmp(signature.data(), expected.data(), expected.size()) != 0)
    return false;
  if (nonces.size() >= 4096) nonces.clear();
  nonces.insert(std::string(nonce));
  return true;
}

std::string_view command_payload(std::string_view input) {
  constexpr std::string_view marker = "{\"payload\":";
  const auto auth = input.rfind(",\"auth\":{");
  if (!input.starts_with(marker) || auth == std::string_view::npos) return {};
  return input.substr(marker.size(), auth - marker.size());
}

std::string_view json_string_field(std::string_view input, std::string_view name,
                                   std::size_t maximum) {
  const std::string marker = "\"" + std::string(name) + "\":\"";
  const auto start = input.find(marker);
  if (start == std::string_view::npos) return {};
  const auto value = start + marker.size();
  const auto end = input.find('"', value);
  if (end == std::string_view::npos || end - value > maximum) return {};
  const auto result = input.substr(value, end - value);
  if (result.find('\\') != std::string_view::npos) return {};
  return result;
}

bool valid_command_id(std::string_view value) {
  if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' ||
      value[23] != '-')
    return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) continue;
    const auto character = value[index];
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
          (character >= 'A' && character <= 'F')))
      return false;
  }
  return true;
}

bool json_integer_field(std::string_view input, std::string_view name, std::int64_t& result) {
  const std::string marker = "\"" + std::string(name) + "\":";
  const auto start = input.find(marker);
  if (start == std::string_view::npos) return false;
  const auto value = start + marker.size();
  auto end = value;
  if (end < input.size() && input[end] == '-') ++end;
  while (end < input.size() && input[end] >= '0' && input[end] <= '9') ++end;
  if (end == value || (end == value + 1 && input[value] == '-')) return false;
  try {
    std::size_t parsed{};
    result = std::stoll(std::string(input.substr(value, end - value)), &parsed);
    return parsed == end - value;
  } catch (...) {
    return false;
  }
}

void post_json(std::string_view host, std::uint16_t port, std::string_view path,
               std::string_view body) noexcept {
  struct SocketGuard {
    ~SocketGuard() {
      if (descriptor >= 0) ::close(descriptor);
    }
    void reset(int next = -1) noexcept {
      if (descriptor >= 0) ::close(descriptor);
      descriptor = next;
    }
    int descriptor{-1};
  } socket;

  try {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* raw_addresses{};
    const auto service = std::to_string(port);
    const auto host_name = std::string(host);
    if (::getaddrinfo(host_name.c_str(), service.c_str(), &hints, &raw_addresses) != 0) return;
    const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(raw_addresses,
                                                                         &::freeaddrinfo);
    for (auto* address = addresses.get(); address; address = address->ai_next) {
      socket.reset(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
      if (socket.descriptor < 0) continue;
#ifdef SO_NOSIGPIPE
      int no_sigpipe = 1;
      ::setsockopt(socket.descriptor, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
      timeval timeout{0, 500000};
      ::setsockopt(socket.descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
      ::setsockopt(socket.descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      const int flags = ::fcntl(socket.descriptor, F_GETFL, 0);
      if (flags >= 0) ::fcntl(socket.descriptor, F_SETFL, flags | O_NONBLOCK);
      int connected = ::connect(socket.descriptor, address->ai_addr, address->ai_addrlen);
      if (connected != 0 && errno == EINPROGRESS) {
        pollfd descriptor{socket.descriptor, POLLOUT, 0};
        if (::poll(&descriptor, 1, 500) > 0) {
          int error{};
          socklen_t size = sizeof(error);
          if (::getsockopt(socket.descriptor, SOL_SOCKET, SO_ERROR, &error, &size) == 0 &&
              error == 0)
            connected = 0;
        }
      }
      if (connected == 0) {
        if (flags >= 0) ::fcntl(socket.descriptor, F_SETFL, flags);
        break;
      }
      socket.reset();
    }
    if (socket.descriptor < 0) return;
    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\nHost: " << host << ':' << port
            << "\r\nContent-Type: application/json\r\nContent-Length: " << body.size()
            << "\r\nConnection: close\r\n\r\n"
            << body;
    const auto data = request.str();
    std::size_t offset{};
    while (offset < data.size()) {
      const auto sent = ::send(socket.descriptor, data.data() + offset, data.size() - offset,
#ifdef MSG_NOSIGNAL
                               MSG_NOSIGNAL
#else
                               0
#endif
      );
      if (sent <= 0) break;
      offset += static_cast<std::size_t>(sent);
    }
  } catch (...) {
    // OTLP export is best effort and must not terminate or block graph processing.
  }
}

}  // namespace

std::string_view to_string(ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::disconnected:
      return "disconnected";
    case ConnectionState::connecting:
      return "connecting";
    case ConnectionState::listening:
      return "listening";
    case ConnectionState::connected:
      return "connected";
    case ConnectionState::closed:
      return "closed";
    case ConnectionState::error:
      return "error";
  }
  return "unknown";
}

std::string_view to_string(UdpEvent event) noexcept {
  switch (event) {
    case UdpEvent::malformed:
      return "malformed";
    case UdpEvent::truncated:
      return "truncated";
    case UdpEvent::oversized:
      return "oversized";
    case UdpEvent::socket_error:
      return "socket_error";
    case UdpEvent::sequence_gap:
      return "sequence_gap";
    case UdpEvent::duplicate:
      return "duplicate";
    case UdpEvent::out_of_order:
      return "out_of_order";
  }
  return "unknown";
}

void MetricsTraceSink::on_send(std::string_view edge_id, const Envelope&, std::size_t wire_bytes) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.sent;
  value.sent_wire_bytes += wire_bytes;
}

void MetricsTraceSink::on_receive(std::string_view edge_id, const Envelope&, std::size_t wire_bytes,
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

void MetricsTraceSink::on_backpressure(std::string_view edge_id, std::chrono::nanoseconds duration,
                                       bool rejected) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  ++value.backpressure_events;
  value.total_backpressure += duration;
  if (rejected) ++value.rejected;
}

void MetricsTraceSink::on_udp_event(std::string_view edge_id, UdpEvent event, std::uint64_t count) {
  std::scoped_lock lock(mutex_);
  auto& value = edges_[std::string(edge_id)];
  switch (event) {
    case UdpEvent::malformed:
      value.udp_malformed += count;
      break;
    case UdpEvent::truncated:
      value.udp_truncated += count;
      break;
    case UdpEvent::oversized:
      value.udp_oversized += count;
      break;
    case UdpEvent::socket_error:
      value.udp_socket_errors += count;
      break;
    case UdpEvent::sequence_gap:
      value.udp_sequence_gaps += count;
      break;
    case UdpEvent::duplicate:
      value.udp_duplicates += count;
      break;
    case UdpEvent::out_of_order:
      value.udp_out_of_order += count;
      break;
  }
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
                                    std::size_t wire_bytes, std::chrono::nanoseconds latency) {
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

void CompositeTraceSink::on_udp_event(std::string_view edge_id, UdpEvent event,
                                      std::uint64_t count) {
  for (auto* sink : sinks_) sink->on_udp_event(edge_id, event, count);
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
  std::string shared_secret;
  int socket{-1};
  std::atomic_bool paused{};
  std::atomic_bool stopping{};
  std::thread control_thread;
};

UdpJsonTraceSink::UdpJsonTraceSink(std::string node_id, std::string host, std::uint16_t port,
                                   std::string shared_secret)
    : impl_(std::make_unique<Impl>()) {
  impl_->node_id = std::move(node_id);
  impl_->shared_secret = std::move(shared_secret);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* addresses{};
  const auto service = std::to_string(port);
  if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) return;
  for (auto* address = addresses; address; address = address->ai_next) {
    const int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (candidate >= 0 && ::connect(candidate, address->ai_addr, address->ai_addrlen) == 0) {
      impl_->socket = candidate;
      break;
    }
    if (candidate >= 0) ::close(candidate);
  }
  ::freeaddrinfo(addresses);
  if (impl_->socket >= 0) {
    impl_->control_thread = std::thread([state = impl_.get()] {
      pollfd descriptor{state->socket, POLLIN, 0};
      std::array<char, 2048> buffer{};
      std::unordered_set<std::string> nonces;
      while (!state->stopping.load(std::memory_order_relaxed)) {
        descriptor.revents = 0;
        const auto ready = ::poll(&descriptor, 1, 100);
        if (ready <= 0 || !(descriptor.revents & POLLIN)) continue;
        const auto count = ::recv(state->socket, buffer.data(), buffer.size(), 0);
        if (count <= 0) continue;
        const std::string_view command(buffer.data(), static_cast<std::size_t>(count));
        if (!authenticate_command(command, state->shared_secret, nonces)) continue;
        const auto payload = command_payload(command);
        const auto kind = json_string_field(payload, "kind", 16);
        const auto command_id = json_string_field(payload, "commandId", 36);
        const auto target_node = json_string_field(payload, "targetNode", 64);
        const auto requested_action = json_string_field(payload, "action", 8);
        std::int64_t expires_at{};
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        if (kind != "control" || !valid_command_id(command_id) || target_node != state->node_id ||
            !json_integer_field(payload, "expiresAt", expires_at) || expires_at < now ||
            expires_at > now + 30000)
          continue;
        std::string_view action;
        if (requested_action == "pause") {
          state->paused.store(true, std::memory_order_relaxed);
          action = "pause";
        } else if (requested_action == "resume") {
          state->paused.store(false, std::memory_order_relaxed);
          action = "resume";
        }
        if (!action.empty()) {
          const auto acknowledgement_payload =
              std::string{"{\"kind\":\"control_ack\",\"nodeId\":\""} + escape_json(state->node_id) +
              "\",\"action\":\"" + std::string(action) + "\",\"commandId\":\"" +
              std::string(command_id) + "\",\"accepted\":true,\"state\":\"" +
              (state->paused.load(std::memory_order_relaxed) ? "paused" : "running") + "\"}";
          const auto acknowledgement =
              signed_datagram(acknowledgement_payload, state->shared_secret);
          ::send(state->socket, acknowledgement.data(), acknowledgement.size(), 0);
        }
      }
    });
  }
}

UdpJsonTraceSink::~UdpJsonTraceSink() {
  if (!impl_) return;
  impl_->stopping.store(true, std::memory_order_relaxed);
  if (impl_->control_thread.joinable()) impl_->control_thread.join();
  if (impl_->socket >= 0) ::close(impl_->socket);
}

bool UdpJsonTraceSink::paused() const noexcept {
  return impl_ && impl_->paused.load(std::memory_order_relaxed);
}

void UdpJsonTraceSink::on_send(std::string_view edge_id, const Envelope& envelope,
                               std::size_t wire_bytes) {
  emit("send", edge_id, &envelope, wire_bytes, {});
}

void UdpJsonTraceSink::on_receive(std::string_view edge_id, const Envelope& envelope,
                                  std::size_t wire_bytes, std::chrono::nanoseconds latency) {
  emit("receive", edge_id, &envelope, wire_bytes, latency);
}

void UdpJsonTraceSink::on_error(std::string_view edge_id, std::string_view message) {
  emit("error", edge_id, nullptr, 0, {}, message);
}

void UdpJsonTraceSink::on_connection(std::string_view edge_id, ConnectionState state) {
  emit("connection", edge_id, nullptr, 0, {}, to_string(state));
}

void UdpJsonTraceSink::on_reconnect(std::string_view edge_id) {
  emit("reconnect", edge_id, nullptr, 0, {});
}

void UdpJsonTraceSink::on_backpressure(std::string_view edge_id, std::chrono::nanoseconds duration,
                                       bool rejected) {
  emit("backpressure", edge_id, nullptr, 0, duration, rejected ? "rejected" : "blocked");
}

void UdpJsonTraceSink::on_processing(std::string_view, const Envelope& envelope,
                                     std::chrono::nanoseconds duration, bool success) {
  emit("processing", {}, &envelope, 0, duration, success ? "ok" : "error");
}

void UdpJsonTraceSink::on_heartbeat(std::string_view, double cpu_percent) {
  emit("heartbeat", {}, nullptr, 0, {}, {}, cpu_percent);
}

void UdpJsonTraceSink::on_capture(std::string_view edge_id, const Envelope& envelope,
                                  std::string_view direction, std::string_view file,
                                  std::uint64_t packet_index, std::uint64_t file_offset) {
  if (!impl_ || impl_->socket < 0) return;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  std::ostringstream json;
  json << "{\"kind\":\"capture\",\"event\":\"frame\",\"nodeId\":\"" << escape_json(impl_->node_id)
       << "\",\"edgeId\":\"" << escape_json(edge_id) << "\",\"timestamp\":" << now
       << ",\"sequence\":" << envelope.sequence
       << ",\"wireVersion\":" << static_cast<unsigned>(envelope.wire_version) << ",\"type\":\""
       << escape_json(envelope.type) << "\",\"messageId\":\"" << escape_json(envelope.message_id)
       << "\",\"parentMessageId\":\"" << escape_json(envelope.parent_message_id)
       << "\",\"traceId\":\"" << escape_json(envelope.trace_id) << "\",\"direction\":\""
       << escape_json(direction) << "\",\"captureFile\":\"" << escape_json(file)
       << "\",\"capturePacket\":" << packet_index << ",\"captureOffset\":" << file_offset << '}';
  const auto value = signed_datagram(json.str(), impl_->shared_secret);
  ::send(impl_->socket, value.data(), value.size(), 0);
}

void UdpJsonTraceSink::emit(std::string_view event, std::string_view edge_id,
                            const Envelope* envelope, std::size_t wire_bytes,
                            std::chrono::nanoseconds latency, std::string_view message,
                            double cpu_percent) {
  if (!impl_ || impl_->socket < 0) return;
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  std::ostringstream json;
  json << "{\"kind\":\"trace\",\"event\":\"" << event << "\",\"nodeId\":\""
       << escape_json(impl_->node_id) << '"';
  if (!edge_id.empty()) json << ",\"edgeId\":\"" << escape_json(edge_id) << '"';
  json << ",\"timestamp\":" << now << ",\"wireBytes\":" << wire_bytes << ",\"latencyUs\":"
       << std::chrono::duration_cast<std::chrono::microseconds>(latency).count();
  if (envelope) {
    json << ",\"sequence\":" << envelope->sequence
         << ",\"wireVersion\":" << static_cast<unsigned>(envelope->wire_version) << ",\"type\":\""
         << escape_json(envelope->type) << "\",\"payloadBytes\":" << envelope->payload.size()
         << ",\"messageId\":\"" << escape_json(envelope->message_id) << "\",\"parentMessageId\":\""
         << escape_json(envelope->parent_message_id) << "\",\"traceId\":\""
         << escape_json(envelope->trace_id) << '"';
  }
  if (!message.empty()) json << ",\"message\":\"" << escape_json(message) << '"';
  if (cpu_percent >= 0.0) json << ",\"cpuPercent\":" << std::setprecision(15) << cpu_percent;
  json << '}';
  const auto value = signed_datagram(json.str(), impl_->shared_secret);
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

OtlpHttpTraceSink::OtlpHttpTraceSink(std::string node_id, std::string host, std::uint16_t port,
                                     std::string path, std::size_t queue_capacity)
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
                                   std::size_t wire_bytes, std::chrono::nanoseconds latency) {
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
                                     const Envelope* envelope, std::chrono::nanoseconds duration,
                                     std::string_view status, std::size_t wire_bytes) {
  if (!impl_) return;
  const auto end = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const auto start = end - std::max<std::int64_t>(0, duration.count());
  const auto trace_source = envelope ? std::string_view(envelope->trace_id) : subject;
  const auto trace_id =
      is_canonical_identity(trace_source) ? std::string(trace_source) : hex_id(trace_source, 32);
  const auto span_id = generate_span_id();
  std::ostringstream json;
  json << "{\"resourceSpans\":[{\"resource\":{\"attributes\":[{\"key\":\"service.name\","
          "\"value\":{\"stringValue\":\"graphx-"
       << escape_json(impl_->node_id)
       << "\"}}]},\"scopeSpans\":[{\"scope\":{\"name\":\"graphx.runtime\"},\"spans\":[{"
          "\"traceId\":\""
       << trace_id << "\",\"spanId\":\"" << span_id << "\",\"name\":\"" << name << ' '
       << escape_json(subject) << "\",\"startTimeUnixNano\":\"" << start
       << "\",\"endTimeUnixNano\":\"" << end
       << "\",\"attributes\":[{\"key\":\"graphx.subject\",\"value\":{\"stringValue\":\""
       << escape_json(subject) << "\"}},{\"key\":\"graphx.status\",\"value\":{\"stringValue\":\""
       << escape_json(status) << "\"}},{\"key\":\"graphx.wire_bytes\",\"value\":{\"intValue\":\""
       << wire_bytes << "\"}}";
  if (envelope) {
    json << ",{\"key\":\"graphx.sequence\",\"value\":{\"intValue\":\"" << envelope->sequence
         << "\"}},{\"key\":\"graphx.message_id\",\"value\":{"
            "\"stringValue\":\""
         << escape_json(envelope->message_id)
         << "\"}},{\"key\":\"graphx.parent_message_id\","
            "\"value\":{\"stringValue\":\""
         << escape_json(envelope->parent_message_id) << "\"}}";
  }
  json << "]}]}]}]}";
  {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->stopping || impl_->queue.size() >= impl_->capacity) return;
    impl_->queue.push_back(json.str());
  }
  impl_->ready.notify_one();
}

}  // namespace graphx
