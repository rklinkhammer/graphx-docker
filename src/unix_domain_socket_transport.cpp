#include "graphx/unix_domain_socket_transport.hpp"

#include "graphx/framing.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace graphx {
namespace {

using Clock = std::chrono::steady_clock;

class ScopedSocket final {
 public:
  explicit ScopedSocket(int descriptor) noexcept : descriptor_(descriptor) {}
  ScopedSocket(const ScopedSocket&) = delete;
  ScopedSocket& operator=(const ScopedSocket&) = delete;
  ~ScopedSocket() {
    if (descriptor_ >= 0) ::close(descriptor_);
  }
  [[nodiscard]] int get() const noexcept { return descriptor_; }
  [[nodiscard]] int release() noexcept {
    const int descriptor = descriptor_;
    descriptor_ = -1;
    return descriptor;
  }

 private:
  int descriptor_;
};

std::runtime_error socket_error(std::string_view action, int error = errno) {
  return std::runtime_error(std::string(action) + ": " + std::strerror(error));
}

sockaddr_un address_for(const std::string& path) {
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
    throw std::invalid_argument("Unix-domain socket path is empty or too long");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return address;
}

void validate_options(const UnixDomainSocketOptions& options) {
  constexpr auto maximum = std::chrono::minutes(10);
  if (options.connect_timeout <= std::chrono::milliseconds::zero() ||
      options.connect_timeout > maximum)
    throw std::invalid_argument("Unix socket connect timeout must be between 1 and 600000 ms");
  if (options.send_timeout <= std::chrono::milliseconds::zero() || options.send_timeout > maximum)
    throw std::invalid_argument("Unix socket send timeout must be between 1 and 600000 ms");
}

int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

void configure_socket(int socket, bool nonblocking) {
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
    throw socket_error("configure Unix socket SIGPIPE protection");
#endif
  if (nonblocking) {
    const int flags = ::fcntl(socket, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0)
      throw socket_error("configure nonblocking Unix socket");
  }
}

int poll_timeout(Clock::time_point deadline, bool has_deadline) {
  if (!has_deadline) return -1;
  const auto remaining = deadline - Clock::now();
  if (remaining <= Clock::duration::zero()) return 0;
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      remaining + std::chrono::milliseconds(1));
  return static_cast<int>(
      std::min<std::int64_t>(milliseconds.count(), std::numeric_limits<int>::max()));
}

bool wait_ready(int socket, short events, Clock::time_point deadline, bool has_deadline) {
  for (;;) {
    pollfd descriptor{socket, events, 0};
    const int status = ::poll(&descriptor, 1, poll_timeout(deadline, has_deadline));
    if (status > 0) {
      if (descriptor.revents & POLLNVAL) throw socket_error("poll Unix socket", EBADF);
      return true;
    }
    if (status == 0) return false;
    if (errno != EINTR) throw socket_error("poll Unix socket");
  }
}

enum class WaitStatus { ready, timeout, cancelled };

WaitStatus wait_ready_or_cancel(int socket, short events, int cancel, Clock::time_point deadline,
                                bool has_deadline) {
  for (;;) {
    std::array<pollfd, 2> descriptors{{{socket, events, 0}, {cancel, POLLIN, 0}}};
    const int status =
        ::poll(descriptors.data(), descriptors.size(), poll_timeout(deadline, has_deadline));
    if (status > 0) {
      if (descriptors[1].revents != 0) return WaitStatus::cancelled;
      if (descriptors[0].revents & POLLNVAL) throw socket_error("poll Unix socket", EBADF);
      return WaitStatus::ready;
    }
    if (status == 0) return WaitStatus::timeout;
    if (errno != EINTR) throw socket_error("poll Unix socket");
  }
}

std::chrono::nanoseconds write_all(int socket, int cancel, std::span<const std::byte> bytes,
                                   Clock::time_point deadline) {
  pollfd probe{socket, POLLOUT, 0};
  const auto pressure_start = Clock::now();
  const bool pressured = ::poll(&probe, 1, 0) == 0;
  while (!bytes.empty()) {
    const auto ready = wait_ready_or_cancel(socket, POLLOUT, cancel, deadline, true);
    if (ready == WaitStatus::cancelled) throw socket_error("Unix socket send cancelled", ECANCELED);
    if (ready == WaitStatus::timeout) throw socket_error("Unix socket send timed out", ETIMEDOUT);
    const auto sent = ::send(socket, bytes.data(), bytes.size(), send_flags());
    if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (sent <= 0) throw socket_error("Unix socket send", sent == 0 ? EPIPE : errno);
    bytes = bytes.subspan(static_cast<std::size_t>(sent));
  }
  return pressured ? Clock::now() - pressure_start : std::chrono::nanoseconds{};
}

enum class ReadStatus { complete, closed, timed_out, cancelled };
struct ReadResult {
  ReadStatus status;
  std::size_t transferred;
};

ReadResult read_all(int socket, int cancel, std::span<std::byte> bytes, Clock::time_point deadline,
                    bool has_deadline) {
  std::size_t transferred{};
  while (!bytes.empty()) {
    const auto ready = wait_ready_or_cancel(socket, POLLIN, cancel, deadline, has_deadline);
    if (ready == WaitStatus::cancelled) return {ReadStatus::cancelled, transferred};
    if (ready == WaitStatus::timeout) return {ReadStatus::timed_out, transferred};
    const auto count = ::recv(socket, bytes.data(), bytes.size(), 0);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count < 0) throw socket_error("Unix socket receive");
    if (count == 0) return {ReadStatus::closed, transferred};
    const auto amount = static_cast<std::size_t>(count);
    transferred += amount;
    bytes = bytes.subspan(amount);
  }
  return {ReadStatus::complete, transferred};
}

}  // namespace

UnixDomainSocketTransport::UnixDomainSocketTransport(int socket, int listener, std::string edge_id,
                                                     TraceSink* trace_sink,
                                                     UnixDomainSocketOptions options,
                                                     std::string owned_path)
    : socket_(socket),
      listener_(listener),
      edge_id_(std::move(edge_id)),
      trace_sink_(trace_sink),
      options_(options),
      owned_path_(std::move(owned_path)) {
  if (!trace_sink_) trace_sink_ = &null_trace_sink_;
  int cancellation[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, cancellation) != 0) {
    if (socket >= 0) ::close(socket);
    if (listener >= 0) ::close(listener);
    if (!owned_path_.empty()) ::unlink(owned_path_.c_str());
    throw socket_error("create Unix socket cancellation pair");
  }
  try {
    configure_socket(cancellation[0], true);
    configure_socket(cancellation[1], true);
  } catch (...) {
    ::close(cancellation[0]);
    ::close(cancellation[1]);
    if (socket >= 0) ::close(socket);
    if (listener >= 0) ::close(listener);
    if (!owned_path_.empty()) ::unlink(owned_path_.c_str());
    throw;
  }
  cancel_read_.store(cancellation[0]);
  cancel_write_.store(cancellation[1]);
}

UnixDomainSocketTransport UnixDomainSocketTransport::connect(std::string path, std::string edge_id,
                                                             TraceSink* trace_sink) {
  return connect(std::move(path), std::move(edge_id), trace_sink, {});
}

UnixDomainSocketTransport UnixDomainSocketTransport::connect(std::string path, std::string edge_id,
                                                             TraceSink* trace_sink,
                                                             UnixDomainSocketOptions options) {
  validate_options(options);
  const int socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket < 0) throw socket_error("create Unix socket");
  if (trace_sink) trace_sink->on_connection(edge_id, ConnectionState::connecting);
  try {
    configure_socket(socket, true);
    const auto address = address_for(path);
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      if (errno != EINPROGRESS) throw socket_error("connect Unix socket");
      const auto deadline = Clock::now() + options.connect_timeout;
      if (!wait_ready(socket, POLLOUT, deadline, true))
        throw socket_error("connect Unix socket timed out", ETIMEDOUT);
      int error{};
      socklen_t size = sizeof(error);
      if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0)
        throw socket_error("connect Unix socket", error == 0 ? errno : error);
    }
  } catch (...) {
    ::close(socket);
    throw;
  }
  auto transport = UnixDomainSocketTransport(socket, -1, std::move(edge_id), trace_sink, options);
  transport.trace_sink_->on_connection(transport.edge_id_, ConnectionState::connected);
  return transport;
}

UnixDomainSocketTransport UnixDomainSocketTransport::listen(std::string path, std::string edge_id,
                                                            TraceSink* trace_sink) {
  return listen(std::move(path), std::move(edge_id), trace_sink, {});
}

UnixDomainSocketTransport UnixDomainSocketTransport::listen(std::string path, std::string edge_id,
                                                            TraceSink* trace_sink,
                                                            UnixDomainSocketOptions options) {
  validate_options(options);
  ::unlink(path.c_str());
  const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0) throw socket_error("create Unix listener");
  try {
    configure_socket(listener, true);
    const auto address = address_for(path);
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 1) != 0)
      throw socket_error("listen on Unix socket");
  } catch (...) {
    ::close(listener);
    ::unlink(path.c_str());
    throw;
  }
  auto transport = UnixDomainSocketTransport(-1, listener, std::move(edge_id), trace_sink, options,
                                             std::move(path));
  transport.trace_sink_->on_connection(transport.edge_id_, ConnectionState::listening);
  return transport;
}

UnixDomainSocketTransport::UnixDomainSocketTransport(UnixDomainSocketTransport&& other) noexcept
    : socket_(other.socket_.exchange(-1)),
      listener_(other.listener_.exchange(-1)),
      cancel_read_(other.cancel_read_.exchange(-1)),
      cancel_write_(other.cancel_write_.exchange(-1)),
      closed_(other.closed_.load()),
      edge_id_(std::move(other.edge_id_)),
      trace_sink_(other.trace_sink_),
      options_(other.options_),
      owned_path_(std::move(other.owned_path_)) {
  other.closed_.store(true);
  other.owned_path_.clear();
  if (other.trace_sink_ == &other.null_trace_sink_) trace_sink_ = &null_trace_sink_;
}

UnixDomainSocketTransport& UnixDomainSocketTransport::operator=(
    UnixDomainSocketTransport&& other) noexcept {
  if (this == &other) return *this;
  close();
  const int old_cancel_read = cancel_read_.exchange(-1);
  if (old_cancel_read >= 0) ::close(old_cancel_read);
  const int old_cancel_write = cancel_write_.exchange(-1);
  if (old_cancel_write >= 0) ::close(old_cancel_write);
  socket_.store(other.socket_.exchange(-1));
  listener_.store(other.listener_.exchange(-1));
  cancel_read_.store(other.cancel_read_.exchange(-1));
  cancel_write_.store(other.cancel_write_.exchange(-1));
  closed_.store(other.closed_.load());
  edge_id_ = std::move(other.edge_id_);
  trace_sink_ =
      other.trace_sink_ == &other.null_trace_sink_ ? &null_trace_sink_ : other.trace_sink_;
  options_ = other.options_;
  owned_path_ = std::move(other.owned_path_);
  other.closed_.store(true);
  other.owned_path_.clear();
  return *this;
}

UnixDomainSocketTransport::~UnixDomainSocketTransport() {
  close();
  const int cancel_read = cancel_read_.exchange(-1);
  if (cancel_read >= 0) ::close(cancel_read);
  const int cancel_write = cancel_write_.exchange(-1);
  if (cancel_write >= 0) ::close(cancel_write);
}

void UnixDomainSocketTransport::send(const Envelope& envelope) {
  if (closed_.load()) throw std::runtime_error("send on closed Unix socket transport");
  const int socket = socket_.load();
  if (socket < 0) throw std::runtime_error("Unix socket listener has no accepted peer");
  const auto serialized = serialize(envelope);
  const auto framed = frame(serialized);
  std::chrono::nanoseconds pressure{};
  try {
    pressure = write_all(socket, cancel_read_.load(), framed, Clock::now() + options_.send_timeout);
  } catch (const std::exception& error) {
    if (!closed_.load()) invalidate_connection();
    report_error(error.what());
    throw;
  }
  try {
    if (pressure.count() > 0) trace_sink_->on_backpressure(edge_id_, pressure, false);
    trace_sink_->on_send(edge_id_, envelope, framed.size());
  } catch (...) {
    // A completed frame remains valid even if its best-effort observer fails.
  }
}

ReceiveResult UnixDomainSocketTransport::receive_result(std::chrono::milliseconds timeout) {
  const bool has_deadline = timeout.count() >= 0;
  const auto deadline = has_deadline ? Clock::now() + timeout : Clock::time_point{};
  if (closed_.load()) return {ReceiveStatus::cancelled, std::nullopt};

  if (socket_.load() < 0) {
    const int listener = listener_.load();
    if (listener < 0) return {ReceiveStatus::end_of_stream, std::nullopt};
    try {
      const auto ready =
          wait_ready_or_cancel(listener, POLLIN, cancel_read_.load(), deadline, has_deadline);
      if (ready == WaitStatus::cancelled) return {ReceiveStatus::cancelled, std::nullopt};
      if (ready == WaitStatus::timeout) return {ReceiveStatus::timeout, std::nullopt};
      int accepted;
      do accepted = ::accept(listener, nullptr, nullptr);
      while (accepted < 0 && errno == EINTR);
      if (accepted < 0) throw socket_error("accept Unix socket");
      ScopedSocket accepted_socket(accepted);
      configure_socket(accepted_socket.get(), true);
      if (closed_.load()) {
        return {ReceiveStatus::cancelled, std::nullopt};
      }
      socket_.store(accepted_socket.release());
      trace_sink_->on_connection(edge_id_, ConnectionState::connected);
    } catch (const std::exception&) {
      if (closed_.load()) return {ReceiveStatus::cancelled, std::nullopt};
      throw;
    }
  }

  std::array<std::byte, 4> prefix{};
  ReadResult header;
  try {
    header = read_all(socket_.load(), cancel_read_.load(), prefix, deadline, has_deadline);
  } catch (const std::exception& error) {
    if (closed_.load()) return {ReceiveStatus::cancelled, std::nullopt};
    invalidate_connection();
    report_error(error.what());
    throw;
  }
  if (closed_.load()) return {ReceiveStatus::cancelled, std::nullopt};
  if (header.status == ReadStatus::cancelled) return {ReceiveStatus::cancelled, std::nullopt};
  if (header.status == ReadStatus::timed_out) {
    if (header.transferred == 0) return {ReceiveStatus::timeout, std::nullopt};
    fail_connection("Unix socket receive timed out during frame header");
  }
  if (header.status == ReadStatus::closed) {
    if (header.transferred != 0) fail_connection("Unix socket closed during frame header");
    invalidate_connection();
    return {ReceiveStatus::end_of_stream, std::nullopt};
  }

  std::uint32_t size{};
  try {
    size = decode_frame_size(prefix);
  } catch (const std::exception& error) {
    fail_connection("invalid Unix socket frame prefix: " + std::string(error.what()));
  }
  std::vector<std::byte> payload(size);
  ReadResult body;
  try {
    body = read_all(socket_.load(), cancel_read_.load(), payload, deadline, has_deadline);
  } catch (const std::exception& error) {
    if (closed_.load()) return {ReceiveStatus::cancelled, std::nullopt};
    invalidate_connection();
    report_error(error.what());
    throw;
  }
  if (closed_.load()) return {ReceiveStatus::cancelled, std::nullopt};
  if (body.status == ReadStatus::cancelled) return {ReceiveStatus::cancelled, std::nullopt};
  if (body.status == ReadStatus::timed_out) {
    fail_connection("Unix socket receive timed out during frame payload");
  }
  if (body.status == ReadStatus::closed) {
    fail_connection("Unix socket closed during frame payload");
  }
  Envelope envelope;
  try {
    envelope = deserialize(payload);
  } catch (const std::exception& error) {
    fail_connection("malformed Unix socket envelope: " + std::string(error.what()));
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  trace_sink_->on_receive(
      edge_id_, envelope, size + 4,
      std::chrono::nanoseconds(std::max<std::int64_t>(0, now_ns - envelope.timestamp_ns)));
  return {ReceiveStatus::message, std::move(envelope)};
}

void UnixDomainSocketTransport::invalidate_connection() noexcept {
  const int socket = socket_.exchange(-1);
  if (socket >= 0) {
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
  }
  const int listener = listener_.exchange(-1);
  if (listener >= 0) {
    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
  }
}

void UnixDomainSocketTransport::report_error(std::string_view message) noexcept {
  try {
    trace_sink_->on_connection(edge_id_, ConnectionState::error);
  } catch (...) {
  }
  try {
    trace_sink_->on_error(edge_id_, message);
  } catch (...) {
    // Preserve the transport failure that caused this diagnostic.
  }
}

[[noreturn]] void UnixDomainSocketTransport::fail_connection(std::string message) {
  invalidate_connection();
  report_error(message);
  throw std::runtime_error(std::move(message));
}

void UnixDomainSocketTransport::close() {
  if (closed_.exchange(true)) return;
  const int cancel_write = cancel_write_.load();
  if (cancel_write >= 0) {
    const std::byte signal{1};
    [[maybe_unused]] const auto written = ::send(cancel_write, &signal, sizeof(signal), 0);
  }
  invalidate_connection();
  try {
    trace_sink_->on_connection(edge_id_, ConnectionState::closed);
  } catch (...) {
    // Destruction must not fail because an observer failed.
  }
  if (!owned_path_.empty()) {
    ::unlink(owned_path_.c_str());
    owned_path_.clear();
  }
}

}  // namespace graphx
