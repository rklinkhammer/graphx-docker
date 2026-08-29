#include "graphx/tcp_transport.hpp"

#include "graphx/framing.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace graphx {
namespace {

using Clock = std::chrono::steady_clock;

std::runtime_error system_error(std::string_view action, int error = errno) {
  return std::runtime_error(std::string(action) + ": " + std::strerror(error));
}

struct AddrinfoDeleter {
  void operator()(addrinfo* value) const noexcept { ::freeaddrinfo(value); }
};
using AddrinfoPtr = std::unique_ptr<addrinfo, AddrinfoDeleter>;

AddrinfoPtr resolve(const Endpoint& endpoint, bool passive) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = passive ? AI_PASSIVE : 0;
  addrinfo* result{};
  const auto service = std::to_string(endpoint.port);
  const char* host = endpoint.host.empty() ? nullptr : endpoint.host.c_str();
  const int status = ::getaddrinfo(host, service.c_str(), &hints, &result);
  if (status != 0) throw std::runtime_error(std::string("resolve: ") + ::gai_strerror(status));
  return AddrinfoPtr(result);
}

void configure_socket(int socket) {
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
    throw system_error("setsockopt SO_NOSIGPIPE");
#else
  (void)socket;
#endif
  const int flags = ::fcntl(socket, F_GETFL, 0);
  if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0)
    throw system_error("configure nonblocking socket");
}

int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

int poll_timeout(Clock::time_point deadline, bool has_deadline) {
  if (!has_deadline) return -1;
  const auto remaining = deadline - Clock::now();
  if (remaining <= Clock::duration::zero()) return 0;
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(remaining +
                                                            std::chrono::milliseconds(1));
  return static_cast<int>(std::min<std::int64_t>(milliseconds.count(),
                                                 std::numeric_limits<int>::max()));
}

bool wait_ready(int socket, short events, Clock::time_point deadline, bool has_deadline) {
  for (;;) {
    pollfd descriptor{socket, events, 0};
    const int status = ::poll(&descriptor, 1, poll_timeout(deadline, has_deadline));
    if (status > 0) {
      if (descriptor.revents & POLLNVAL) throw system_error("poll", EBADF);
      return true;
    }
    if (status == 0) return false;
    if (errno != EINTR) throw system_error("poll");
  }
}

int connect_once(const Endpoint& endpoint, std::chrono::milliseconds timeout) {
  auto addresses = resolve(endpoint, false);
  int last_error = ECONNREFUSED;
  for (auto* address = addresses.get(); address; address = address->ai_next) {
    const int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (candidate < 0) {
      last_error = errno;
      continue;
    }
    try {
      configure_socket(candidate);
      int status = ::connect(candidate, address->ai_addr, address->ai_addrlen);
      if (status != 0 && errno == EINPROGRESS) {
        const auto deadline = Clock::now() + timeout;
        if (!wait_ready(candidate, POLLOUT, deadline, true)) {
          last_error = ETIMEDOUT;
          ::close(candidate);
          continue;
        }
        socklen_t size = sizeof(last_error);
        if (::getsockopt(candidate, SOL_SOCKET, SO_ERROR, &last_error, &size) != 0)
          throw system_error("finish connect");
        status = last_error == 0 ? 0 : -1;
      } else if (status != 0) {
        last_error = errno;
      }
      if (status == 0) return candidate;
    } catch (...) {
      ::close(candidate);
      throw;
    }
    ::close(candidate);
  }
  throw system_error("connect", last_error);
}

int create_listener(const Endpoint& endpoint) {
  auto addresses = resolve(endpoint, true);
  int last_error = EADDRNOTAVAIL;
  for (auto* address = addresses.get(); address; address = address->ai_next) {
    const int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (candidate < 0) {
      last_error = errno;
      continue;
    }
    try {
      configure_socket(candidate);
      int reuse = 1;
      if (::setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
        throw system_error("setsockopt SO_REUSEADDR");
      if (::bind(candidate, address->ai_addr, address->ai_addrlen) == 0 &&
          ::listen(candidate, 16) == 0)
        return candidate;
      last_error = errno;
    } catch (...) {
      ::close(candidate);
      throw;
    }
    ::close(candidate);
  }
  throw system_error("listen", last_error);
}

enum class ReadStatus { complete, closed, timed_out };
struct ReadResult {
  ReadStatus status;
  std::size_t transferred;
};

ReadResult read_all(int socket, std::span<std::byte> bytes, Clock::time_point deadline,
                    bool has_deadline) {
  std::size_t transferred{};
  while (!bytes.empty()) {
    if (!wait_ready(socket, POLLIN, deadline, has_deadline))
      return {ReadStatus::timed_out, transferred};
    const auto count = ::recv(socket, bytes.data(), bytes.size(), 0);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count < 0) throw system_error("receive");
    if (count == 0) return {ReadStatus::closed, transferred};
    const auto amount = static_cast<std::size_t>(count);
    transferred += amount;
    bytes = bytes.subspan(amount);
  }
  return {ReadStatus::complete, transferred};
}

std::chrono::nanoseconds write_all(int socket, std::span<const std::byte> bytes,
                                   Clock::time_point deadline) {
  pollfd probe{socket, POLLOUT, 0};
  const auto pressure_start = Clock::now();
  const bool pressured = ::poll(&probe, 1, 0) == 0;
  while (!bytes.empty()) {
    if (!wait_ready(socket, POLLOUT, deadline, true))
      throw system_error("send timed out", ETIMEDOUT);
    const auto sent = ::send(socket, bytes.data(), bytes.size(), send_flags());
    if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (sent <= 0) throw system_error("send", sent == 0 ? EPIPE : errno);
    bytes = bytes.subspan(static_cast<std::size_t>(sent));
  }
  return pressured ? Clock::now() - pressure_start : std::chrono::nanoseconds{};
}

}  // namespace

TcpTransport::TcpTransport(int socket, int listener, Endpoint endpoint, std::string edge_id,
                           TraceSink* trace_sink, TcpOptions options, bool outbound)
    : socket_(socket), listener_(listener), endpoint_(std::move(endpoint)),
      edge_id_(std::move(edge_id)), trace_sink_(trace_sink), options_(options),
      outbound_(outbound) {
  if (!trace_sink_) trace_sink_ = &null_trace_sink_;
}

std::string TcpTransport::context(std::string_view action) const {
  std::string result = "TCP";
  if (!edge_id_.empty()) result += " edge '" + edge_id_ + "'";
  result += " " + endpoint_.host + ":" + std::to_string(endpoint_.port) + " ";
  result += action;
  return result;
}

void TcpTransport::connect_outbound() {
  const auto attempts = std::max<std::size_t>(1, options_.retry.max_attempts);
  auto backoff = options_.retry.initial_backoff;
  std::string last_error;
  for (std::size_t attempt = 1; attempt <= attempts; ++attempt) {
    if (closed_.load()) throw std::runtime_error(context("connection cancelled"));
    try {
      trace_sink_->on_connection(edge_id_, ConnectionState::connecting);
      const int connected = connect_once(endpoint_, options_.connect_timeout);
      if (closed_.load()) {
        ::close(connected);
        throw std::runtime_error(context("connection cancelled"));
      }
      socket_.store(connected);
      if (ever_connected_) trace_sink_->on_reconnect(edge_id_);
      ever_connected_ = true;
      trace_sink_->on_connection(edge_id_, ConnectionState::connected);
      return;
    } catch (const std::exception& error) {
      last_error = error.what();
      trace_sink_->on_error(edge_id_, context("connect attempt " + std::to_string(attempt) +
                                              " failed: " + last_error));
      if (attempt == attempts) break;
      std::unique_lock lock(retry_mutex_);
      if (retry_ready_.wait_for(lock, backoff, [&] { return closed_.load(); }))
        throw std::runtime_error(context("connection cancelled"));
      backoff = std::min(options_.retry.max_backoff, backoff * 2);
    }
  }
  throw std::runtime_error(context("connect failed after " + std::to_string(attempts) +
                                   " attempt(s): " + last_error));
}

TcpTransport TcpTransport::connect(Endpoint endpoint, std::string edge_id,
                                   TraceSink* trace_sink, TcpOptions options) {
  TcpTransport transport(-1, -1, std::move(endpoint), std::move(edge_id), trace_sink, options,
                         true);
  transport.connect_outbound();
  return transport;
}

TcpTransport TcpTransport::listen(Endpoint endpoint, std::string edge_id,
                                  TraceSink* trace_sink, TcpOptions options) {
  const int listener = create_listener(endpoint);
  TcpTransport transport(-1, listener, std::move(endpoint), std::move(edge_id), trace_sink,
                         options, false);
  transport.trace_sink_->on_connection(transport.edge_id_, ConnectionState::listening);
  return transport;
}

TcpTransport::TcpTransport(TcpTransport&& other) noexcept
    : socket_(other.socket_.exchange(-1)), listener_(other.listener_.exchange(-1)),
      closed_(other.closed_.load()), endpoint_(std::move(other.endpoint_)),
      edge_id_(std::move(other.edge_id_)), trace_sink_(other.trace_sink_), options_(other.options_),
      outbound_(other.outbound_), ever_connected_(other.ever_connected_) {
  other.closed_.store(true);
  if (other.trace_sink_ == &other.null_trace_sink_) trace_sink_ = &null_trace_sink_;
}

TcpTransport& TcpTransport::operator=(TcpTransport&& other) noexcept {
  if (this == &other) return *this;
  close();
  socket_.store(other.socket_.exchange(-1));
  listener_.store(other.listener_.exchange(-1));
  closed_.store(other.closed_.load());
  endpoint_ = std::move(other.endpoint_);
  edge_id_ = std::move(other.edge_id_);
  trace_sink_ = other.trace_sink_ == &other.null_trace_sink_ ? &null_trace_sink_ : other.trace_sink_;
  options_ = other.options_;
  outbound_ = other.outbound_;
  ever_connected_ = other.ever_connected_;
  other.closed_.store(true);
  return *this;
}

TcpTransport::~TcpTransport() { close(); }

void TcpTransport::close_connection() noexcept {
  const int socket = socket_.exchange(-1);
  if (socket >= 0) {
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
    try { trace_sink_->on_connection(edge_id_, ConnectionState::disconnected); }
    catch (...) { /* Closing a socket must remain noexcept. */ }
  }
}

bool TcpTransport::accept_inbound(Clock::time_point deadline, bool has_deadline) {
  if (socket_.load() >= 0) return true;
  const int listener = listener_.load();
  if (listener < 0 || closed_.load()) return false;
  if (!wait_ready(listener, POLLIN, deadline, has_deadline)) return false;
  int accepted;
  do accepted = ::accept(listener, nullptr, nullptr);
  while (accepted < 0 && errno == EINTR);
  if (accepted < 0) {
    const auto message = context("accept failed: " + std::string(system_error("accept").what()));
    trace_sink_->on_error(edge_id_, message);
    throw std::runtime_error(message);
  }
  try {
    configure_socket(accepted);
  } catch (const std::exception& error) {
    ::close(accepted);
    const auto message = context("accepted socket setup failed: " + std::string(error.what()));
    trace_sink_->on_error(edge_id_, message);
    throw std::runtime_error(message);
  }
  if (closed_.load()) {
    ::close(accepted);
    return false;
  }
  socket_.store(accepted);
  if (ever_connected_) trace_sink_->on_reconnect(edge_id_);
  ever_connected_ = true;
  trace_sink_->on_connection(edge_id_, ConnectionState::connected);
  return true;
}

void TcpTransport::send(const Envelope& envelope) {
  std::scoped_lock lock(send_mutex_);
  if (closed_.load()) throw std::runtime_error(context("send on closed transport"));
  const auto serialized = serialize(envelope);
  const auto framed = frame(serialized);
  const int attempts = outbound_ && options_.reconnect ? 2 : 1;
  for (int attempt = 1; attempt <= attempts; ++attempt) {
    try {
      if (socket_.load() < 0) {
        if (!outbound_) throw std::runtime_error("no accepted peer");
        connect_outbound();
      }
      const auto pressure = write_all(socket_.load(), framed,
                                      Clock::now() + options_.send_timeout);
      if (pressure.count() > 0) trace_sink_->on_backpressure(edge_id_, pressure, false);
      trace_sink_->on_send(edge_id_, envelope, framed.size());
      return;
    } catch (const std::exception& error) {
      trace_sink_->on_error(edge_id_, context("send failed: " + std::string(error.what())));
      close_connection();
      if (attempt == attempts)
        throw std::runtime_error(context("send failed: " + std::string(error.what())));
    }
  }
}

std::optional<Envelope> TcpTransport::receive(std::chrono::milliseconds timeout) {
  const bool has_deadline = timeout.count() >= 0;
  const auto deadline = has_deadline ? Clock::now() + timeout : Clock::time_point{};
  const auto failure = [&](std::string detail) {
    auto message = context(detail);
    trace_sink_->on_error(edge_id_, message);
    return std::runtime_error(std::move(message));
  };
  for (;;) {
    if (closed_.load()) throw failure("receive on closed transport");
    if (socket_.load() < 0 && !accept_inbound(deadline, has_deadline)) return std::nullopt;

    std::array<std::byte, 4> prefix{};
    ReadResult header;
    try {
      header = read_all(socket_.load(), prefix, deadline, has_deadline);
    } catch (const std::exception& error) {
      close_connection();
      throw failure("receive header failed: " + std::string(error.what()));
    }
    if (header.status == ReadStatus::timed_out) {
      if (header.transferred == 0) return std::nullopt;
      close_connection();
      throw failure("receive timed out during frame header");
    }
    if (header.status == ReadStatus::closed) {
      close_connection();
      if (header.transferred != 0)
        throw failure("peer closed during frame header");
      if (!outbound_ && options_.reconnect) continue;
      if (!outbound_) {
        const int listener = listener_.exchange(-1);
        if (listener >= 0) ::close(listener);
      }
      return std::nullopt;
    }

    std::uint32_t size{};
    try {
      size = decode_frame_size(prefix);
    } catch (const std::exception& error) {
      close_connection();
      throw failure("invalid frame header: " + std::string(error.what()));
    }
    std::vector<std::byte> payload(size);
    ReadResult body;
    try {
      body = read_all(socket_.load(), payload, deadline, has_deadline);
    } catch (const std::exception& error) {
      close_connection();
      throw failure("receive payload failed: " + std::string(error.what()));
    }
    if (body.status == ReadStatus::timed_out) {
      close_connection();
      throw failure("receive timed out during frame payload");
    }
    if (body.status == ReadStatus::closed) {
      close_connection();
      throw failure("peer closed during frame payload");
    }

    Envelope envelope;
    try {
      envelope = deserialize(payload);
    } catch (const std::exception& error) {
      throw failure("malformed envelope: " + std::string(error.what()));
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    trace_sink_->on_receive(edge_id_, envelope, size + 4,
                            std::chrono::nanoseconds(std::max<std::int64_t>(
                                0, now_ns - envelope.timestamp_ns)));
    return envelope;
  }
}

void TcpTransport::close() {
  if (closed_.exchange(true)) return;
  retry_ready_.notify_all();
  close_connection();
  const int listener = listener_.exchange(-1);
  if (listener >= 0) {
    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
  }
  try { trace_sink_->on_connection(edge_id_, ConnectionState::closed); }
  catch (...) { /* Destruction must not fail because an observer failed. */ }
}

}  // namespace graphx
