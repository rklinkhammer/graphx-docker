#include "graphx/tcp_transport.hpp"

#include "graphx/framing.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace graphx {
namespace {

std::runtime_error socket_error(std::string_view action) {
  return std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}

addrinfo* resolve(const Endpoint& endpoint, bool passive) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = passive ? AI_PASSIVE : 0;
  addrinfo* result{};
  const auto service = std::to_string(endpoint.port);
  const char* host = endpoint.host.empty() ? nullptr : endpoint.host.c_str();
  const int status = getaddrinfo(host, service.c_str(), &hints, &result);
  if (status != 0) throw std::runtime_error(std::string("resolve: ") + gai_strerror(status));
  return result;
}

void write_all(int socket, std::span<const std::byte> bytes) {
  while (!bytes.empty()) {
    const auto sent = ::send(socket, bytes.data(), bytes.size(), 0);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) throw socket_error("send");
    bytes = bytes.subspan(static_cast<std::size_t>(sent));
  }
}

bool read_all(int socket, std::span<std::byte> bytes) {
  while (!bytes.empty()) {
    const auto count = ::recv(socket, bytes.data(), bytes.size(), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) throw socket_error("receive");
    if (count == 0) return false;
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
  return true;
}

}  // namespace

TcpTransport::TcpTransport(int socket, std::string edge_id, TraceSink* trace_sink)
    : socket_(socket), edge_id_(std::move(edge_id)), trace_sink_(trace_sink) {
  if (!trace_sink_) trace_sink_ = &null_trace_sink_;
}

TcpTransport TcpTransport::connect(Endpoint endpoint, std::string edge_id,
                                   TraceSink* trace_sink) {
  addrinfo* addresses = resolve(endpoint, false);
  int connected = -1;
  for (auto* address = addresses; address; address = address->ai_next) {
    int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (candidate >= 0 && ::connect(candidate, address->ai_addr, address->ai_addrlen) == 0) {
      connected = candidate;
      break;
    }
    if (candidate >= 0) ::close(candidate);
  }
  freeaddrinfo(addresses);
  if (connected < 0) throw socket_error("connect");
  return TcpTransport(connected, std::move(edge_id), trace_sink);
}

TcpTransport TcpTransport::listen(Endpoint endpoint, std::string edge_id,
                                  TraceSink* trace_sink) {
  addrinfo* addresses = resolve(endpoint, true);
  int listener = -1;
  for (auto* address = addresses; address; address = address->ai_next) {
    int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (candidate < 0) continue;
    int reuse = 1;
    setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (::bind(candidate, address->ai_addr, address->ai_addrlen) == 0 &&
        ::listen(candidate, 1) == 0) {
      listener = candidate;
      break;
    }
    ::close(candidate);
  }
  freeaddrinfo(addresses);
  if (listener < 0) throw socket_error("listen");
  const int accepted = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (accepted < 0) throw socket_error("accept");
  return TcpTransport(accepted, std::move(edge_id), trace_sink);
}

TcpTransport::TcpTransport(TcpTransport&& other) noexcept
    : socket_(other.socket_), edge_id_(std::move(other.edge_id_)), trace_sink_(other.trace_sink_) {
  other.socket_ = -1;
  if (other.trace_sink_ == &other.null_trace_sink_) trace_sink_ = &null_trace_sink_;
}

TcpTransport& TcpTransport::operator=(TcpTransport&& other) noexcept {
  if (this == &other) return *this;
  close();
  socket_ = other.socket_;
  edge_id_ = std::move(other.edge_id_);
  trace_sink_ = other.trace_sink_ == &other.null_trace_sink_ ? &null_trace_sink_ : other.trace_sink_;
  other.socket_ = -1;
  return *this;
}

TcpTransport::~TcpTransport() { close(); }

void TcpTransport::send(const Envelope& envelope) {
  const auto serialized = serialize(envelope);
  const auto framed = frame(serialized);
  write_all(socket_, framed);
  trace_sink_->on_send(edge_id_, envelope, framed.size());
}

std::optional<Envelope> TcpTransport::receive(std::chrono::milliseconds timeout) {
  if (timeout.count() >= 0) {
    pollfd descriptor{socket_, POLLIN, 0};
    int status;
    do status = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    while (status < 0 && errno == EINTR);
    if (status == 0) return std::nullopt;
    if (status < 0) throw socket_error("poll");
  }

  std::array<std::byte, 4> prefix{};
  if (!read_all(socket_, prefix)) return std::nullopt;
  const auto size = decode_frame_size(prefix);
  std::vector<std::byte> payload(size);
  if (!read_all(socket_, payload)) throw std::runtime_error("connection closed mid-frame");
  auto envelope = deserialize(payload);
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  trace_sink_->on_receive(edge_id_, envelope, size + 4,
                          std::chrono::nanoseconds(std::max<std::int64_t>(0, now_ns - envelope.timestamp_ns)));
  return envelope;
}

void TcpTransport::close() {
  if (socket_ >= 0) {
    ::shutdown(socket_, SHUT_RDWR);
    ::close(socket_);
    socket_ = -1;
  }
}

}  // namespace graphx
