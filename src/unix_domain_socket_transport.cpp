#include "graphx/unix_domain_socket_transport.hpp"

#include "graphx/framing.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace graphx {
namespace {

std::runtime_error socket_error(std::string_view action) {
  return std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}

sockaddr_un address_for(const std::string& path) {
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
    throw std::invalid_argument("Unix-domain socket path is empty or too long");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return address;
}

void write_all(int socket, std::span<const std::byte> bytes) {
  while (!bytes.empty()) {
    const auto sent = ::send(socket, bytes.data(), bytes.size(), 0);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) throw socket_error("Unix socket send");
    bytes = bytes.subspan(static_cast<std::size_t>(sent));
  }
}

bool read_all(int socket, std::span<std::byte> bytes) {
  while (!bytes.empty()) {
    const auto count = ::recv(socket, bytes.data(), bytes.size(), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) throw socket_error("Unix socket receive");
    if (count == 0) return false;
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
  return true;
}

}  // namespace

UnixDomainSocketTransport::UnixDomainSocketTransport(
    int socket, std::string edge_id, TraceSink* trace_sink, std::string owned_path)
    : socket_(socket), edge_id_(std::move(edge_id)), trace_sink_(trace_sink),
      owned_path_(std::move(owned_path)) {
  if (!trace_sink_) trace_sink_ = &null_trace_sink_;
}

UnixDomainSocketTransport UnixDomainSocketTransport::connect(
    std::string path, std::string edge_id, TraceSink* trace_sink) {
  const int socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket < 0) throw socket_error("create Unix socket");
  const auto address = address_for(path);
  if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    const auto error = socket_error("connect Unix socket");
    ::close(socket);
    throw error;
  }
  return UnixDomainSocketTransport(socket, std::move(edge_id), trace_sink);
}

UnixDomainSocketTransport UnixDomainSocketTransport::listen(
    std::string path, std::string edge_id, TraceSink* trace_sink) {
  ::unlink(path.c_str());
  const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0) throw socket_error("create Unix listener");
  const auto address = address_for(path);
  if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 1) != 0) {
    const auto error = socket_error("listen on Unix socket");
    ::close(listener);
    ::unlink(path.c_str());
    throw error;
  }
  const int accepted = ::accept(listener, nullptr, nullptr);
  ::close(listener);
  if (accepted < 0) {
    ::unlink(path.c_str());
    throw socket_error("accept Unix socket");
  }
  return UnixDomainSocketTransport(accepted, std::move(edge_id), trace_sink,
                                   std::move(path));
}

UnixDomainSocketTransport::UnixDomainSocketTransport(
    UnixDomainSocketTransport&& other) noexcept
    : socket_(other.socket_), edge_id_(std::move(other.edge_id_)),
      trace_sink_(other.trace_sink_), owned_path_(std::move(other.owned_path_)) {
  other.socket_ = -1;
  other.owned_path_.clear();
  if (other.trace_sink_ == &other.null_trace_sink_) trace_sink_ = &null_trace_sink_;
}

UnixDomainSocketTransport& UnixDomainSocketTransport::operator=(
    UnixDomainSocketTransport&& other) noexcept {
  if (this == &other) return *this;
  close();
  socket_ = other.socket_;
  edge_id_ = std::move(other.edge_id_);
  trace_sink_ = other.trace_sink_ == &other.null_trace_sink_ ? &null_trace_sink_
                                                             : other.trace_sink_;
  owned_path_ = std::move(other.owned_path_);
  other.socket_ = -1;
  other.owned_path_.clear();
  return *this;
}

UnixDomainSocketTransport::~UnixDomainSocketTransport() { close(); }

void UnixDomainSocketTransport::send(const Envelope& envelope) {
  const auto serialized = serialize(envelope);
  const auto framed = frame(serialized);
  write_all(socket_, framed);
  trace_sink_->on_send(edge_id_, envelope, framed.size());
}

std::optional<Envelope> UnixDomainSocketTransport::receive(
    std::chrono::milliseconds timeout) {
  if (timeout.count() >= 0) {
    pollfd descriptor{socket_, POLLIN, 0};
    int status;
    do status = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    while (status < 0 && errno == EINTR);
    if (status == 0) return std::nullopt;
    if (status < 0) throw socket_error("poll Unix socket");
  }
  std::array<std::byte, 4> prefix{};
  if (!read_all(socket_, prefix)) return std::nullopt;
  const auto size = decode_frame_size(prefix);
  std::vector<std::byte> payload(size);
  if (!read_all(socket_, payload)) throw std::runtime_error("Unix socket closed mid-frame");
  auto envelope = deserialize(payload);
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  trace_sink_->on_receive(edge_id_, envelope, size + 4,
                          std::chrono::nanoseconds(std::max<std::int64_t>(
                              0, now_ns - envelope.timestamp_ns)));
  return envelope;
}

void UnixDomainSocketTransport::close() {
  if (socket_ >= 0) {
    ::shutdown(socket_, SHUT_RDWR);
    ::close(socket_);
    socket_ = -1;
  }
  if (!owned_path_.empty()) {
    ::unlink(owned_path_.c_str());
    owned_path_.clear();
  }
}

}  // namespace graphx
