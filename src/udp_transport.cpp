#include "graphx/udp_transport.hpp"

#include "graphx/framing.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits>
#include <memory>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace graphx {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kSequenceWindow = 256;
constexpr auto kSocketDiagnosticInterval = std::chrono::seconds(1);

std::runtime_error system_error(std::string_view action, int error = errno) {
  return std::runtime_error(std::string(action) + ": " + std::strerror(error));
}

in_addr parse_address(std::string_view value, std::string_view label) {
  in_addr address{};
  const std::string text(value);
  if (::inet_pton(AF_INET, text.c_str(), &address) != 1)
    throw std::invalid_argument(std::string(label) + " must be an IPv4 address");
  return address;
}

bool is_multicast(in_addr address) noexcept {
  const auto host = ntohl(address.s_addr);
  return (host & 0xf0000000U) == 0xe0000000U;
}

bool is_limited_broadcast(in_addr address) noexcept { return ntohl(address.s_addr) == 0xffffffffU; }

in_addr interface_address(const std::string& interface) {
  if (interface.empty()) return in_addr{htonl(INADDR_ANY)};
  in_addr literal{};
  if (::inet_pton(AF_INET, interface.c_str(), &literal) == 1) return literal;

  ifaddrs* raw{};
  if (::getifaddrs(&raw) != 0) throw system_error("getifaddrs");
  std::unique_ptr<ifaddrs, decltype(&::freeifaddrs)> values(raw, &::freeifaddrs);
  for (auto* current = values.get(); current; current = current->ifa_next) {
    if (!current->ifa_addr || current->ifa_addr->sa_family != AF_INET ||
        interface != current->ifa_name)
      continue;
    return reinterpret_cast<sockaddr_in*>(current->ifa_addr)->sin_addr;
  }
  throw std::invalid_argument("UDP interface '" + interface + "' has no configured IPv4 address");
}

sockaddr_in endpoint_address(const Endpoint& endpoint, std::string_view label) {
  if (endpoint.port == 0) throw std::invalid_argument(std::string(label) + " port must be nonzero");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  address.sin_addr = parse_address(endpoint.host, label);
  return address;
}

void set_nonblocking_cloexec(int descriptor) {
  const int status_flags = ::fcntl(descriptor, F_GETFL, 0);
  if (status_flags < 0 || ::fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0)
    throw system_error("configure nonblocking descriptor");
  const int descriptor_flags = ::fcntl(descriptor, F_GETFD, 0);
  if (descriptor_flags < 0 || ::fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0)
    throw system_error("configure close-on-exec descriptor");
}

void set_integer_option(int socket, int level, int name, std::uint32_t value,
                        std::string_view label) {
  if (value > static_cast<std::uint32_t>(INT_MAX))
    throw std::invalid_argument(std::string(label) + " exceeds platform socket limit");
  const int option = static_cast<int>(value);
  if (::setsockopt(socket, level, name, &option, sizeof(option)) != 0)
    throw system_error(std::string("setsockopt ") + std::string(label));
}

int poll_timeout(Clock::time_point deadline, bool finite) {
  if (!finite) return -1;
  const auto remaining = deadline - Clock::now();
  if (remaining <= Clock::duration::zero()) return 0;
  const auto rounded = std::chrono::duration_cast<std::chrono::milliseconds>(
      remaining + std::chrono::milliseconds(1));
  return static_cast<int>(std::min<std::int64_t>(rounded.count(), std::numeric_limits<int>::max()));
}

std::chrono::nanoseconds envelope_latency(const Envelope& envelope) {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  return std::chrono::nanoseconds(std::max<std::int64_t>(0, now_ns - envelope.timestamp_ns));
}

}  // namespace

struct UdpTransport::Impl {
  std::atomic<int> socket{-1};
  std::array<std::atomic<int>, 2> cancellation{-1, -1};
  std::atomic<bool> closed{false};
  bool sender{};
  sockaddr_in target{};
  ip_mreq membership{};
  bool joined{};
  std::string endpoint_text;
  std::string edge_id;
  TraceSink* trace_sink{};
  NullTraceSink null_trace_sink;
  UdpOptions options;
  std::vector<std::byte> receive_buffer;
  std::mutex close_mutex;
  std::mutex send_mutex;
  std::mutex receive_mutex;
  std::mutex diagnostic_mutex;
  bool resources_closed{};
  bool socket_diagnostic_emitted{};
  Clock::time_point last_socket_diagnostic{};
  bool sequence_initialized{};
  std::uint64_t highest_sequence{};
  std::array<std::uint64_t, kSequenceWindow> recent_sequences{};
  std::size_t recent_count{};
  std::size_t recent_cursor{};

  ~Impl() { close_noexcept(); }

  std::string context(std::string_view action) const {
    std::string result = "UDP edge '" + edge_id + "' " + std::string(action);
    if (!endpoint_text.empty()) result += " (" + endpoint_text + ")";
    return result;
  }

  void event(UdpEvent event_value, std::uint64_t count = 1) noexcept {
    try {
      trace_sink->on_udp_event(edge_id, event_value, count);
    } catch (...) {
    }
  }

  void connection(ConnectionState state) noexcept {
    try {
      trace_sink->on_connection(edge_id, state);
    } catch (...) {
    }
  }

  void sent(const Envelope& envelope, std::size_t bytes) noexcept {
    try {
      trace_sink->on_send(edge_id, envelope, bytes);
    } catch (...) {
    }
  }

  void received(const Envelope& envelope, std::size_t bytes) noexcept {
    try {
      trace_sink->on_receive(edge_id, envelope, bytes, envelope_latency(envelope));
    } catch (...) {
    }
  }

  void socket_failure(std::string_view action, int error) noexcept {
    event(UdpEvent::socket_error);
    bool report{};
    {
      std::scoped_lock lock(diagnostic_mutex);
      const auto now = Clock::now();
      if (!socket_diagnostic_emitted || now - last_socket_diagnostic >= kSocketDiagnosticInterval) {
        socket_diagnostic_emitted = true;
        last_socket_diagnostic = now;
        report = true;
      }
    }
    if (!report) return;
    try {
      trace_sink->on_error(edge_id, context(std::string(action) + ": " + std::strerror(error)));
    } catch (...) {
    }
  }

  void track_sequence(std::uint64_t sequence) noexcept {
    const auto recent_end = recent_sequences.begin() + static_cast<std::ptrdiff_t>(recent_count);
    if (std::find(recent_sequences.begin(), recent_end, sequence) != recent_end) {
      event(UdpEvent::duplicate);
      return;
    }
    if (sequence_initialized) {
      if (sequence < highest_sequence) {
        event(UdpEvent::out_of_order);
      } else if (highest_sequence != std::numeric_limits<std::uint64_t>::max() &&
                 sequence > highest_sequence + 1) {
        event(UdpEvent::sequence_gap, sequence - highest_sequence - 1);
      }
    }
    if (!sequence_initialized || sequence > highest_sequence) highest_sequence = sequence;
    sequence_initialized = true;
    if (recent_count < kSequenceWindow) {
      recent_sequences[recent_count++] = sequence;
    } else {
      recent_sequences[recent_cursor] = sequence;
      recent_cursor = (recent_cursor + 1) % kSequenceWindow;
    }
  }

  void close_noexcept() noexcept {
    std::unique_lock close_lock(close_mutex);
    if (resources_closed) return;
    closed.store(true);
    if (const int wake = cancellation[1].load(); wake >= 0) {
      const std::byte value{1};
      (void)::send(wake, &value, 1, 0);
    }
    {
      // Keep every descriptor alive until in-flight send/receive calls release
      // their operation locks. In particular, the cancellation read end must
      // remain valid while a blocked poll wakes and observes closed.
      std::scoped_lock operation_locks(send_mutex, receive_mutex);
      const int descriptor = socket.exchange(-1);
      if (descriptor >= 0) {
        if (joined)
          (void)::setsockopt(descriptor, IPPROTO_IP, IP_DROP_MEMBERSHIP, &membership,
                             sizeof(membership));
        (void)::shutdown(descriptor, SHUT_RDWR);
        (void)::close(descriptor);
      }
      for (auto& cancellation_socket : cancellation) {
        const int value = cancellation_socket.exchange(-1);
        if (value >= 0) (void)::close(value);
      }
    }
    resources_closed = true;
    close_lock.unlock();
    connection(ConnectionState::closed);
  }
};

namespace {

std::unique_ptr<UdpTransport::Impl> create_impl(Endpoint endpoint, std::string destination,
                                                std::string edge_id, TraceSink* trace_sink,
                                                UdpOptions options, bool sender) {
  if (options.max_datagram_bytes < 64 || options.max_datagram_bytes > kMaxUdpDatagramBytes)
    throw std::invalid_argument("UDP max_datagram_bytes must be between 64 and 65507");
  if (options.receive_buffer_bytes < 4096 || options.send_buffer_bytes < 4096)
    throw std::invalid_argument("UDP socket buffers must be at least 4096 bytes");

  auto impl = std::make_unique<UdpTransport::Impl>();
  impl->sender = sender;
  impl->edge_id = std::move(edge_id);
  impl->trace_sink = trace_sink ? trace_sink : &impl->null_trace_sink;
  impl->options = std::move(options);
  impl->endpoint_text = endpoint.host + ":" + std::to_string(endpoint.port);

  const auto endpoint_value = endpoint_address(endpoint, sender ? "destination" : "bind");
  const auto destination_address =
      parse_address(sender ? endpoint.host : destination, "destination");
  if (impl->options.mode == UdpMode::multicast && !is_multicast(destination_address))
    throw std::invalid_argument("UDP multicast destination must be in 224.0.0.0/4");
  if (impl->options.mode == UdpMode::unicast &&
      (is_multicast(destination_address) || is_limited_broadcast(destination_address)))
    throw std::invalid_argument("UDP unicast destination must not be multicast or broadcast");
  if (impl->options.mode == UdpMode::broadcast && is_multicast(destination_address))
    throw std::invalid_argument("UDP broadcast destination must not be multicast");

  const int descriptor = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (descriptor < 0) throw system_error("create UDP socket");
  impl->socket.store(descriptor);
  try {
    set_nonblocking_cloexec(descriptor);
    std::array<int, 2> cancellation{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, cancellation.data()) != 0)
      throw system_error("create UDP cancellation socket");
    impl->cancellation[0].store(cancellation[0]);
    impl->cancellation[1].store(cancellation[1]);
    set_nonblocking_cloexec(cancellation[0]);
    set_nonblocking_cloexec(cancellation[1]);
    set_integer_option(descriptor, SOL_SOCKET, SO_RCVBUF, impl->options.receive_buffer_bytes,
                       "SO_RCVBUF");
    set_integer_option(descriptor, SOL_SOCKET, SO_SNDBUF, impl->options.send_buffer_bytes,
                       "SO_SNDBUF");

    if (impl->options.reuse_address) {
      const int enabled = 1;
      if (::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0)
        throw system_error("setsockopt SO_REUSEADDR");
#ifdef SO_REUSEPORT
      if (::setsockopt(descriptor, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled)) != 0)
        throw system_error("setsockopt SO_REUSEPORT");
#endif
    }

    const auto selected_interface = interface_address(impl->options.interface);
    if (sender) {
      sockaddr_in local{};
      local.sin_family = AF_INET;
      local.sin_port = 0;
      local.sin_addr = parse_address(destination, "bind");
      if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        const int error = errno;
        impl->socket_failure("bind UDP sender", error);
        throw system_error("bind UDP sender", error);
      }
      impl->target = endpoint_value;
      if (impl->options.mode == UdpMode::broadcast) {
        const int enabled = 1;
        if (::setsockopt(descriptor, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0)
          throw system_error("setsockopt SO_BROADCAST");
      } else if (impl->options.mode == UdpMode::multicast) {
        if (::setsockopt(descriptor, IPPROTO_IP, IP_MULTICAST_IF, &selected_interface,
                         sizeof(selected_interface)) != 0)
          throw system_error("setsockopt IP_MULTICAST_IF");
        const unsigned char ttl = impl->options.ttl;
        const unsigned char loopback = impl->options.loopback ? 1 : 0;
        if (::setsockopt(descriptor, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0)
          throw system_error("setsockopt IP_MULTICAST_TTL");
        if (::setsockopt(descriptor, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) !=
            0)
          throw system_error("setsockopt IP_MULTICAST_LOOP");
      }
      impl->connection(ConnectionState::connected);
    } else {
      if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&endpoint_value),
                 sizeof(endpoint_value)) != 0) {
        const int error = errno;
        impl->socket_failure("bind UDP receiver", error);
        throw system_error("bind UDP receiver", error);
      }
      if (impl->options.mode == UdpMode::multicast) {
        impl->membership.imr_multiaddr = destination_address;
        impl->membership.imr_interface = selected_interface;
        if (::setsockopt(descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, &impl->membership,
                         sizeof(impl->membership)) != 0)
          throw system_error("setsockopt IP_ADD_MEMBERSHIP");
        impl->joined = true;
      }
      impl->receive_buffer.resize(impl->options.max_datagram_bytes);
      impl->connection(ConnectionState::listening);
    }
  } catch (...) {
    impl->close_noexcept();
    throw;
  }
  return impl;
}

}  // namespace

UdpTransport::UdpTransport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

UdpTransport UdpTransport::connect(Endpoint destination, std::string bind_address,
                                   std::string edge_id, TraceSink* trace_sink, UdpOptions options) {
  return UdpTransport(create_impl(std::move(destination), std::move(bind_address),
                                  std::move(edge_id), trace_sink, std::move(options), true));
}

UdpTransport UdpTransport::listen(Endpoint bind, std::string destination, std::string edge_id,
                                  TraceSink* trace_sink, UdpOptions options) {
  return UdpTransport(create_impl(std::move(bind), std::move(destination), std::move(edge_id),
                                  trace_sink, std::move(options), false));
}

UdpTransport::UdpTransport(UdpTransport&&) noexcept = default;
UdpTransport& UdpTransport::operator=(UdpTransport&&) noexcept = default;
UdpTransport::~UdpTransport() = default;

void UdpTransport::send(const Envelope& envelope) {
  if (!impl_) throw std::runtime_error("send on moved-from UDP transport");
  std::scoped_lock lock(impl_->send_mutex);
  if (impl_->closed.load()) throw std::runtime_error(impl_->context("send on closed transport"));
  if (serialized_size(envelope) > impl_->options.max_datagram_bytes - 4) {
    impl_->event(UdpEvent::oversized);
    throw std::length_error(impl_->context("framed envelope exceeds max_datagram_bytes"));
  }
  const auto serialized = serialize(envelope);
  const auto framed = frame(serialized);
  const int descriptor = impl_->socket.load();
  const auto sent =
      ::sendto(descriptor, framed.data(), framed.size(), 0,
               reinterpret_cast<const sockaddr*>(&impl_->target), sizeof(impl_->target));
  if (sent < 0) {
    const int error = errno;
    impl_->socket_failure("send failed", error);
    throw system_error(impl_->context("send failed"), error);
  }
  if (static_cast<std::size_t>(sent) != framed.size()) {
    impl_->event(UdpEvent::socket_error);
    throw std::runtime_error(impl_->context("partial datagram send"));
  }
  impl_->sent(envelope, framed.size());
}

ReceiveResult UdpTransport::receive_result(std::chrono::milliseconds timeout) {
  if (!impl_) return {ReceiveStatus::cancelled, std::nullopt};
  std::scoped_lock lock(impl_->receive_mutex);
  const bool finite = timeout.count() >= 0;
  const auto deadline = finite ? Clock::now() + timeout : Clock::time_point{};
  bool first_poll = true;
  for (;;) {
    if (impl_->closed.load()) return {ReceiveStatus::cancelled, std::nullopt};
    // Preserve a zero-timeout nonblocking probe, but after any datagram has
    // been examined enforce the original absolute deadline independently of
    // socket readiness. Otherwise a continuous stream of invalid datagrams
    // keeps poll(0) readable and can starve this call indefinitely.
    if (!first_poll && finite && Clock::now() >= deadline)
      return {ReceiveStatus::timeout, std::nullopt};
    first_poll = false;
    pollfd descriptors[] = {{impl_->socket.load(), POLLIN, 0},
                            {impl_->cancellation[0].load(), POLLIN, 0}};
    int ready;
    do {
      ready = ::poll(descriptors, 2, poll_timeout(deadline, finite));
      if (ready < 0 && errno == EINTR && finite && Clock::now() >= deadline) {
        if (impl_->closed.load()) return {ReceiveStatus::cancelled, std::nullopt};
        return {ReceiveStatus::timeout, std::nullopt};
      }
    } while (ready < 0 && errno == EINTR && !impl_->closed.load());
    if (impl_->closed.load() || (ready > 0 && (descriptors[1].revents & POLLIN)))
      return {ReceiveStatus::cancelled, std::nullopt};
    if (ready == 0) return {ReceiveStatus::timeout, std::nullopt};
    if (ready < 0) {
      const int error = errno;
      impl_->socket_failure("poll failed", error);
      throw system_error(impl_->context("poll failed"), error);
    }
    if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
      if (impl_->closed.load()) return {ReceiveStatus::cancelled, std::nullopt};
      impl_->socket_failure("receive socket failed", EIO);
      throw std::runtime_error(impl_->context("receive socket failed"));
    }
    if (!(descriptors[0].revents & POLLIN)) continue;

    iovec vector{impl_->receive_buffer.data(), impl_->receive_buffer.size()};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    const auto received = ::recvmsg(impl_->socket.load(), &message, 0);
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      if (impl_->closed.load()) return {ReceiveStatus::cancelled, std::nullopt};
      const int error = errno;
      impl_->socket_failure("receive failed", error);
      throw system_error(impl_->context("receive failed"), error);
    }
    if ((message.msg_flags & MSG_TRUNC) != 0) {
      impl_->event(UdpEvent::truncated);
      continue;
    }
    const auto size = static_cast<std::size_t>(received);
    if (size < 4) {
      impl_->event(UdpEvent::malformed);
      continue;
    }
    std::array<std::byte, 4> prefix{};
    std::copy_n(impl_->receive_buffer.begin(), 4, prefix.begin());
    std::uint32_t payload_size{};
    try {
      payload_size = decode_frame_size(prefix);
    } catch (const std::exception&) {
      impl_->event(UdpEvent::malformed);
      continue;
    }
    if (payload_size == 0 || static_cast<std::size_t>(payload_size) + 4 != size) {
      impl_->event(UdpEvent::malformed);
      continue;
    }
    Envelope envelope;
    try {
      envelope =
          deserialize(std::span<const std::byte>(impl_->receive_buffer.data() + 4, payload_size));
    } catch (const std::exception&) {
      impl_->event(UdpEvent::malformed);
      continue;
    }
    impl_->track_sequence(envelope.sequence);
    impl_->received(envelope, size);
    return {ReceiveStatus::message, std::move(envelope)};
  }
}

void UdpTransport::close() {
  if (impl_) impl_->close_noexcept();
}

}  // namespace graphx
