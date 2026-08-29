#include "graphx/envelope.hpp"
#include "graphx/framing.hpp"
#include "graphx/in_process_transport.hpp"
#include "graphx/observability.hpp"
#include "graphx/tcp_transport.hpp"
#include "graphx/unix_domain_socket_transport.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {

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
