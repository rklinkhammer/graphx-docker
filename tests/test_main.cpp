#include "graphx/envelope.hpp"
#include "graphx/framing.hpp"
#include "graphx/in_process_transport.hpp"
#include "graphx/observability.hpp"
#include "graphx/shared_memory_transport.hpp"
#include "graphx/tcp_transport.hpp"
#include "graphx/unix_domain_socket_transport.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <fcntl.h>
#include <future>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void framing() {
  const std::array payload{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  const auto framed = graphx::frame(payload);
  expect(framed.size() == 7, "framed size");
  expect(graphx::decode_frame_size(std::span<const std::byte, 4>(framed.data(), 4)) == 3,
         "frame prefix");
  expect(framed[4] == payload[0] && framed[6] == payload[2], "frame payload");
}

void envelope_round_trip() {
  auto input = graphx::Envelope::make(42, "Sample", "payload");
  input.timestamp_ns = 123456789;
  input.trace_id = "trace-abc";
  input.attributes = {{"unit", "volts"}, {"sensor", "a"}};
  const auto output = graphx::deserialize(graphx::serialize(input));
  expect(output.sequence == input.sequence, "envelope sequence");
  expect(output.timestamp_ns == input.timestamp_ns, "envelope timestamp");
  expect(output.type == input.type && output.payload == input.payload, "envelope body");
  expect(output.attributes == input.attributes, "envelope attributes");
}

void in_process() {
  auto channel = std::make_shared<graphx::InProcessChannel>();
  graphx::InProcessTransport sender(channel), receiver(channel);
  sender.send(graphx::Envelope::make(7, "Ping", "hello"));
  const auto message = receiver.receive(std::chrono::milliseconds(10));
  expect(message && message->payload == "hello", "in-process delivery");
}

void metrics_sink() {
  graphx::MetricsTraceSink metrics;
  const auto envelope = graphx::Envelope::make(8, "Metric", "value");
  metrics.on_send("metrics-edge", envelope, 64);
  metrics.on_receive("metrics-edge", envelope, 64, std::chrono::microseconds(25));
  metrics.on_error("metrics-edge", "example");
  const auto edge = metrics.edge("metrics-edge");
  expect(edge.sent == 1 && edge.received == 1, "metrics message counters");
  expect(edge.wire_bytes == 128 && edge.errors == 1, "metrics byte/error counters");
  expect(edge.total_latency == std::chrono::microseconds(25), "metrics latency");
}

struct RawListener {
  int socket{-1};
  std::uint16_t port{};

  RawListener() {
    socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) throw std::runtime_error("raw listener socket");
    int reuse = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket, 4) != 0)
      throw std::runtime_error("raw listener bind");
    socklen_t size = sizeof(address);
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0)
      throw std::runtime_error("raw listener name");
    port = ntohs(address.sin_port);
  }
  RawListener(const RawListener&) = delete;
  ~RawListener() {
    if (socket >= 0) ::close(socket);
  }
};

void raw_write_all(int socket, std::span<const std::byte> bytes) {
  while (!bytes.empty()) {
    const auto sent = ::send(socket, bytes.data(), bytes.size(), 0);
    if (sent <= 0) throw std::runtime_error("raw send");
    bytes = bytes.subspan(static_cast<std::size_t>(sent));
  }
}

bool raw_read_all(int socket, std::span<std::byte> bytes) {
  while (!bytes.empty()) {
    const auto count = ::recv(socket, bytes.data(), bytes.size(), 0);
    if (count <= 0) return false;
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
  return true;
}

void raw_read_frame(int socket) {
  std::array<std::byte, 4> prefix{};
  expect(raw_read_all(socket, prefix), "raw frame header");
  std::vector<std::byte> payload(graphx::decode_frame_size(prefix));
  expect(raw_read_all(socket, payload), "raw frame payload");
}

std::vector<std::byte> framed_envelope(std::uint64_t sequence, std::string payload) {
  return graphx::frame(graphx::serialize(graphx::Envelope::make(sequence, "Raw", payload)));
}

std::string shared_segment(std::string_view suffix) {
  static unsigned counter{};
  return "/gx-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) + "-" +
         std::string(suffix);
}

void shared_memory_wraparound_and_cleanup() {
  const auto segment = shared_segment("wrap");
  graphx::SharedMemoryOptions options;
  options.capacity = 3;
  options.max_message_bytes = 4096;
  graphx::MetricsTraceSink metrics;
  auto consumer = graphx::SharedMemoryTransport::listen(segment, "shm-wrap", &metrics, options);
  auto producer = graphx::SharedMemoryTransport::connect(segment, "shm-wrap", &metrics, options);
  try {
    [[maybe_unused]] auto duplicate =
        graphx::SharedMemoryTransport::connect(segment, "shm-duplicate-producer", nullptr, options);
    throw std::runtime_error("second shared-memory producer was accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("live producer") != std::string_view::npos,
           "shared-memory producer ownership");
  }
  for (std::uint64_t sequence = 1; sequence <= 20; ++sequence) {
    producer.send(graphx::Envelope::make(sequence, "Shared", std::to_string(sequence)));
    const auto received = consumer.receive(100ms);
    expect(received && received->sequence == sequence, "shared-memory ring wraparound");
  }
  producer.close();
  expect(!consumer.receive(100ms), "shared-memory close after drain");
  const auto measured = metrics.edge("shm-wrap");
  expect(measured.sent == 20 && measured.received == 20 && measured.wire_bytes > 0,
         "shared-memory tracing hooks");
  consumer.close();
  const int stale = ::shm_open(segment.c_str(), O_RDWR, 0600);
  expect(stale < 0 && errno == ENOENT, "shared-memory owner unlinks segment");
  if (stale >= 0) ::close(stale);
}

void shared_memory_backpressure_and_limits() {
  graphx::SharedMemoryOptions options;
  options.capacity = 1;
  options.max_message_bytes = 256;
  options.backpressure = graphx::SharedMemoryBackpressure::reject;
  const auto reject_segment = shared_segment("reject");
  auto consumer =
      graphx::SharedMemoryTransport::listen(reject_segment, "shm-reject", nullptr, options);
  auto producer =
      graphx::SharedMemoryTransport::connect(reject_segment, "shm-reject", nullptr, options);
  producer.send(graphx::Envelope::make(1, "Shared", "first"));
  try {
    producer.send(graphx::Envelope::make(2, "Shared", "second"));
    throw std::runtime_error("full reject ring accepted a message");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("backpressure=reject") != std::string_view::npos,
           "shared-memory reject policy");
  }
  const auto queued = consumer.receive(100ms);
  expect(queued && queued->sequence == 1, "shared-memory rejected no queued data");
  try {
    producer.send(graphx::Envelope::make(3, "Shared", std::string(512, 'x')));
    throw std::runtime_error("oversized shared-memory frame accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("maximum message size") != std::string_view::npos,
           "shared-memory maximum message size");
  }
  producer.close();
  consumer.close();

  options.backpressure = graphx::SharedMemoryBackpressure::block;
  options.send_timeout = 25ms;
  options.max_message_bytes = 4096;
  const auto block_segment = shared_segment("block");
  auto blocked_consumer =
      graphx::SharedMemoryTransport::listen(block_segment, "shm-block", nullptr, options);
  auto blocked_producer =
      graphx::SharedMemoryTransport::connect(block_segment, "shm-block", nullptr, options);
  blocked_producer.send(graphx::Envelope::make(4, "Shared", "first"));
  try {
    blocked_producer.send(graphx::Envelope::make(5, "Shared", "blocked"));
    throw std::runtime_error("blocked shared-memory send ignored deadline");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("timed out") != std::string_view::npos,
           "shared-memory blocking deadline");
  }
  blocked_producer.close();
  blocked_consumer.close();
}

void shared_memory_receive_timeout_and_settings() {
  const auto segment = shared_segment("timeout");
  graphx::SharedMemoryOptions options;
  options.capacity = 2;
  options.max_message_bytes = 1024;
  auto consumer = graphx::SharedMemoryTransport::listen(segment, "shm-timeout", nullptr, options);
  expect(!consumer.receive(20ms), "empty shared-memory receive timeout");
  try {
    [[maybe_unused]] auto duplicate =
        graphx::SharedMemoryTransport::listen(segment, "shm-duplicate", nullptr, options);
    throw std::runtime_error("live shared-memory consumer was replaced");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("live consumer") != std::string_view::npos,
           "shared-memory live owner protection");
  }
  auto mismatch = options;
  mismatch.capacity = 3;
  try {
    [[maybe_unused]] auto ignored =
        graphx::SharedMemoryTransport::connect(segment, "shm-mismatch", nullptr, mismatch);
    throw std::runtime_error("mismatched shared-memory layout accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("do not match") != std::string_view::npos,
           "shared-memory layout validation");
  }
  consumer.close();
}

void shared_memory_cross_process_and_crash_detection() {
  const auto segment = shared_segment("process");
  graphx::SharedMemoryOptions options;
  options.capacity = 4;
  options.max_message_bytes = 4096;
  auto consumer = graphx::SharedMemoryTransport::listen(segment, "shm-process", nullptr, options);
  const pid_t child = ::fork();
  if (child < 0) throw std::runtime_error("fork shared-memory producer");
  if (child == 0) {
    try {
      auto producer =
          graphx::SharedMemoryTransport::connect(segment, "shm-process", nullptr, options);
      producer.send(graphx::Envelope::make(77, "Shared", "from child"));
      ::_exit(0);  // Deliberately skip cleanup to exercise peer-death detection.
    } catch (...) {
      ::_exit(2);
    }
  }
  const auto received = consumer.receive(2s);
  expect(received && received->sequence == 77 && received->payload == "from child",
         "cross-process shared-memory delivery");
  int status{};
  expect(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
         "shared-memory child status");
  expect(!consumer.receive(2s), "shared-memory producer crash detection");
  consumer.close();

  const auto stale_segment = shared_segment("stale");
  const pid_t stale_child = ::fork();
  if (stale_child < 0) throw std::runtime_error("fork stale shared-memory listener");
  if (stale_child == 0) {
    try {
      [[maybe_unused]] auto stale = graphx::SharedMemoryTransport::listen(
          stale_segment, "shm-stale", nullptr, options);
      ::_exit(0);
    } catch (...) {
      ::_exit(2);
    }
  }
  expect(::waitpid(stale_child, &status, 0) == stale_child && WIFEXITED(status) &&
             WEXITSTATUS(status) == 0,
         "stale shared-memory listener status");
  auto replacement =
      graphx::SharedMemoryTransport::listen(stale_segment, "shm-replacement", nullptr, options);
  replacement.close();
}

void tcp_fragmented_and_consecutive_frames() {
  RawListener listener;
  std::promise<void> first_fragment_sent;
  std::promise<void> continue_send;
  auto release = continue_send.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    const auto first = framed_envelope(11, "fragmented");
    const auto second = framed_envelope(12, "consecutive");
    raw_write_all(peer, std::span(first).first(2));
    first_fragment_sent.set_value();
    release.wait();
    raw_write_all(peer, std::span(first).subspan(2, 5));
    raw_write_all(peer, std::span(first).subspan(7));
    raw_write_all(peer, second);
    ::shutdown(peer, SHUT_RDWR);
    ::close(peer);
  });
  auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "fragmented");
  first_fragment_sent.get_future().wait();
  auto pending = std::async(std::launch::async, [&] { return client.receive(2s); });
  expect(pending.wait_for(20ms) == std::future_status::timeout,
         "partial header must not produce a frame");
  continue_send.set_value();
  const auto first = pending.get();
  const auto second = client.receive(2s);
  expect(first && first->sequence == 11 && first->payload == "fragmented",
         "fragmented frame delivery");
  expect(second && second->sequence == 12 && second->payload == "consecutive",
         "consecutive frame delivery");
  expect(!client.receive(2s), "peer closure between frames");
  server.join();
}

void tcp_truncated_and_oversized_frames() {
  auto run = [](std::vector<std::byte> bytes, std::string_view expected) {
    RawListener listener;
    std::thread server([&] {
      const int peer = ::accept(listener.socket, nullptr, nullptr);
      raw_write_all(peer, bytes);
      ::shutdown(peer, SHUT_RDWR);
      ::close(peer);
    });
    auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "bad-frame");
    try {
      [[maybe_unused]] auto ignored = client.receive(2s);
      throw std::runtime_error("invalid frame was accepted");
    } catch (const std::exception& error) {
      expect(std::string_view(error.what()).find(expected) != std::string_view::npos,
             "contextual frame error");
      expect(std::string_view(error.what()).find("bad-frame") != std::string_view::npos,
             "edge id in frame error");
    }
    server.join();
  };

  run({std::byte{0}, std::byte{0}}, "header");
  run({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{8}, std::byte{1}}, "payload");
  run({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}},
      "malformed envelope");
  const auto too_large = graphx::kMaxFrameBytes + 1;
  run({static_cast<std::byte>((too_large >> 24) & 0xff),
       static_cast<std::byte>((too_large >> 16) & 0xff),
       static_cast<std::byte>((too_large >> 8) & 0xff),
       static_cast<std::byte>(too_large & 0xff)},
      "exceeds configured maximum");
}

void tcp_receive_timeout_covers_partial_frame() {
  RawListener listener;
  std::promise<void> release_server;
  auto release = release_server.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    raw_write_all(peer, {reinterpret_cast<const std::byte*>("\0\0"), 2});
    release.wait();
    ::close(peer);
  });
  auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "deadline");
  try {
    [[maybe_unused]] auto ignored = client.receive(30ms);
    throw std::runtime_error("partial frame timeout was accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("timed out during frame header") !=
               std::string_view::npos,
           "full-frame receive deadline");
  }
  release_server.set_value();
  server.join();
}

void tcp_receive_timeout_without_data() {
  RawListener listener;
  std::promise<void> peer_ready;
  std::promise<void> release_server;
  auto release = release_server.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    peer_ready.set_value();
    release.wait();
    ::close(peer);
  });
  auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "idle-timeout");
  peer_ready.get_future().wait();
  expect(!client.receive(20ms), "idle receive timeout returns no envelope");
  release_server.set_value();
  server.join();
}

void tcp_send_backpressure_has_deadline() {
  RawListener listener;
  std::promise<void> receiver_ready;
  std::promise<void> release_server;
  auto release = release_server.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    int receive_buffer = 1024;
    ::setsockopt(peer, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
    receiver_ready.set_value();
    release.wait();
    ::close(peer);
  });
  graphx::TcpOptions options;
  options.send_timeout = 30ms;
  auto client =
      graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "backpressure", nullptr, options);
  receiver_ready.get_future().wait();
  try {
    client.send(graphx::Envelope::make(19, "Large", std::string(15 * 1024 * 1024, 'x')));
    throw std::runtime_error("blocked send did not respect its deadline");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("timed out") != std::string_view::npos,
           "bounded blocking backpressure");
  }
  release_server.set_value();
  server.join();
}

void tcp_reconnects_without_sigpipe() {
  RawListener listener;
  std::promise<void> first_closed;
  std::thread server([&] {
    int peer = ::accept(listener.socket, nullptr, nullptr);
    raw_read_frame(peer);
    ::shutdown(peer, SHUT_RDWR);
    ::close(peer);
    first_closed.set_value();
    peer = ::accept(listener.socket, nullptr, nullptr);
    raw_read_frame(peer);
    ::close(peer);
  });
  graphx::TcpOptions options;
  options.reconnect = true;
  options.retry = {20, 5ms, 20ms};
  auto client =
      graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "reconnect", nullptr, options);
  client.send(graphx::Envelope::make(20, "Reconnect", "first"));
  first_closed.get_future().wait();
  expect(!client.receive(2s), "closure marks outbound connection disconnected");
  client.send(graphx::Envelope::make(21, "Reconnect", "second"));
  server.join();
}

void tcp_listener_reaccepts_and_close_cancels() {
  std::uint16_t port;
  {
    RawListener reservation;
    port = reservation.port;
  }
  graphx::TcpOptions options;
  options.reconnect = true;
  auto listener = graphx::TcpTransport::listen({"127.0.0.1", port}, "listener", nullptr, options);
  auto received = std::async(std::launch::async, [&] {
    const auto first = listener.receive(2s);
    const auto second = listener.receive(2s);
    return first && second && first->sequence == 31 && second->sequence == 32;
  });
  {
    auto client = graphx::TcpTransport::connect({"127.0.0.1", port}, "first-client");
    client.send(graphx::Envelope::make(31, "Reconnect", "first"));
  }
  {
    auto client = graphx::TcpTransport::connect({"127.0.0.1", port}, "second-client");
    client.send(graphx::Envelope::make(32, "Reconnect", "second"));
  }
  expect(received.get(), "listener accepts a replacement connection");

  auto cancelled = std::async(std::launch::async, [&] {
    try {
      [[maybe_unused]] auto ignored = listener.receive();
    } catch (const std::exception&) {
      return true;
    }
    return false;
  });
  listener.close();
  expect(cancelled.wait_for(1s) == std::future_status::ready && cancelled.get(),
         "close cancels a blocked listener receive");
}

void tcp_end_to_end() {
  const auto port = static_cast<std::uint16_t>(42000 + (::getpid() % 1000));
  std::exception_ptr server_error;
  std::thread server([&] {
    try {
      auto receiver = graphx::TcpTransport::listen({"127.0.0.1", port}, "test");
      auto envelope = receiver.receive(std::chrono::seconds(2));
      expect(envelope && envelope->payload == "over tcp", "tcp delivery");
      envelope->payload = "ack";
      receiver.send(*envelope);
    } catch (...) {
      server_error = std::current_exception();
    }
  });

  try {
    std::optional<graphx::TcpTransport> sender;
    std::string connect_error;
    for (int attempt = 0; attempt < 20 && !sender; ++attempt) {
      try { sender.emplace(graphx::TcpTransport::connect({"127.0.0.1", port}, "test")); }
      catch (const std::exception& error) { connect_error = error.what(); std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
    }
    if (!sender) throw std::runtime_error("tcp server did not become ready: " + connect_error);
    sender->send(graphx::Envelope::make(9, "Ping", "over tcp"));
    auto reply = sender->receive(std::chrono::seconds(2));
    expect(reply && reply->payload == "ack" && reply->sequence == 9, "tcp reply");
  } catch (...) {
    if (server.joinable()) server.join();
    throw;
  }
  server.join();
  if (server_error) std::rethrow_exception(server_error);
}

void unix_socket_end_to_end() {
  const auto path = "/tmp/graphx-test-" + std::to_string(::getpid()) + ".sock";
  std::exception_ptr server_error;
  std::thread server([&] {
    try {
      auto receiver = graphx::UnixDomainSocketTransport::listen(path, "test-uds");
      auto envelope = receiver.receive(std::chrono::seconds(2));
      expect(envelope && envelope->payload == "over uds", "Unix socket delivery");
      envelope->payload = "ack";
      receiver.send(*envelope);
    } catch (...) { server_error = std::current_exception(); }
  });
  try {
    std::optional<graphx::UnixDomainSocketTransport> sender;
    std::string connect_error;
    for (int attempt = 0; attempt < 20 && !sender; ++attempt) {
      try { sender.emplace(graphx::UnixDomainSocketTransport::connect(path, "test-uds")); }
      catch (const std::exception& error) {
        connect_error = error.what();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
    if (!sender) throw std::runtime_error("Unix socket did not become ready: " + connect_error);
    sender->send(graphx::Envelope::make(10, "Ping", "over uds"));
    auto reply = sender->receive(std::chrono::seconds(2));
    expect(reply && reply->payload == "ack", "Unix socket reply");
  } catch (...) {
    if (server.joinable()) server.join();
    throw;
  }
  server.join();
  if (server_error) std::rethrow_exception(server_error);
}

}  // namespace

int main() {
  const std::pair<const char*, std::function<void()>> tests[] = {
      {"framing", framing}, {"envelope", envelope_round_trip},
      {"in-process", in_process}, {"metrics", metrics_sink},
      {"shared-memory wraparound", shared_memory_wraparound_and_cleanup},
      {"shared-memory pressure", shared_memory_backpressure_and_limits},
      {"shared-memory timeout", shared_memory_receive_timeout_and_settings},
      {"shared-memory process", shared_memory_cross_process_and_crash_detection},
      {"TCP fragmented and consecutive", tcp_fragmented_and_consecutive_frames},
      {"TCP truncated and oversized", tcp_truncated_and_oversized_frames},
      {"TCP partial-frame timeout", tcp_receive_timeout_covers_partial_frame},
      {"TCP idle timeout", tcp_receive_timeout_without_data},
      {"TCP send backpressure", tcp_send_backpressure_has_deadline},
      {"TCP reconnect and SIGPIPE", tcp_reconnects_without_sigpipe},
      {"TCP listener lifecycle", tcp_listener_reaccepts_and_close_cancels},
      {"tcp end-to-end", tcp_end_to_end},
      {"Unix socket end-to-end", unix_socket_end_to_end}};
  int failures{};
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "[pass] " << name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[fail] " << name << ": " << error.what() << '\n';
    }
  }
  return failures == 0 ? 0 : 1;
}
