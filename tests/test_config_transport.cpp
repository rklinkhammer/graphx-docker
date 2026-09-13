#include "config_test_support.hpp"

using namespace std::chrono_literals;
using namespace config_test;

namespace {

template <typename Settings>
concept HasTls = requires(Settings settings) { settings.tls; };

template <typename Settings>
concept HasUdpMode = requires(Settings settings) { settings.mode; };

template <typename Settings>
concept HasQueueCapacity = requires(Settings settings) { settings.capacity; };

static_assert(HasTls<graphx::TcpTransportConfig>);
static_assert(!HasTls<graphx::UdpTransportConfig>);
static_assert(!HasTls<graphx::UnixSocketTransportConfig>);
static_assert(HasUdpMode<graphx::UdpTransportConfig>);
static_assert(!HasUdpMode<graphx::TcpTransportConfig>);
static_assert(HasQueueCapacity<graphx::InProcessTransportConfig>);
static_assert(HasQueueCapacity<graphx::SharedMemoryTransportConfig>);
static_assert(!HasQueueCapacity<graphx::TcpTransportConfig>);
static_assert(!HasQueueCapacity<graphx::UdpTransportConfig>);
static_assert(!HasQueueCapacity<graphx::UnixSocketTransportConfig>);
static_assert(std::variant_size_v<decltype(graphx::ExternalTransportConfig::protocol)> == 2);

template <typename Settings>
const Settings& transport_as(const graphx::TransportSettings& transport) {
  return std::get<Settings>(transport);
}

void resolved_transport_settings() {
  const auto c = load_value(authored());
  const auto& tcp = transport_as<graphx::TcpTransportConfig>(c.edge("samples").transport);
  expect(tcp.port > 0 && tcp.connect_timeout_ms == 5000, "TCP defaults");
  auto v = authored();
  v["connections"]["samples"]["settings"]["port"] = 70000;
  rejected(v, "E_SCHEMA", "port");
  v = authored();
  v["connections"]["samples"]["settings"]["ttl"] = 2;
  rejected(v, "E_TRANSPORT", "ttl");
  v = authored();
  v["connections"]["samples"]["settings"]["retry"] = graphx::ConfigValue::Object{
      {"max_attempts", 3}, {"initial_backoff_ms", 100}, {"max_backoff_ms", 1}};
  rejected(v, "E_TRANSPORT", "retry");
  v = authored("udp-multicast");
  v["connections"]["messages"]["settings"]["destination"] = "127.0.0.1";
  rejected(v, "E_TRANSPORT", "destination");
  v = authored("udp-broadcast");
  v["connections"]["messages"]["settings"]["destination"] = "10.0.0.255";
  rejected(v, "E_TRANSPORT", "destination");
}
void in_process_factory_shares_named_channel() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "local";
  edge.transport = graphx::InProcessTransportConfig{.channel = "local-channel"};
  auto sender = factory.create(edge, graphx::ConnectionMode::connect);
  auto receiver = factory.create(edge, graphx::ConnectionMode::listen);
  sender->send(graphx::Envelope::make(4, "Test", "factory"));
  const auto message = receiver->receive(20ms);
  expect(message && message->payload == "factory", "factory in-process delivery");
  std::get<graphx::InProcessTransportConfig>(edge.transport).capacity = 2;
  try {
    [[maybe_unused]] auto inconsistent = factory.create(edge, graphx::ConnectionMode::listen);
    throw std::runtime_error("inconsistent named channel settings were accepted");
  } catch (const std::invalid_argument& error) {
    expect(std::string_view(error.what()).find("inconsistent") != std::string_view::npos,
           "factory rejects inconsistent in-process settings");
  }
}

void factory_rejects_unvalidated_settings() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "invalid";
  edge.transport = graphx::TcpTransportConfig{};
  try {
    [[maybe_unused]] auto ignored = factory.create(edge, graphx::ConnectionMode::connect);
    throw std::runtime_error("invalid factory settings were accepted");
  } catch (const std::invalid_argument&) {
  }
}

void socket_factory_round_trip(graphx::TransportKind kind) {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = kind == graphx::TransportKind::tcp ? "factory-tcp" : "factory-unix";
  if (kind == graphx::TransportKind::tcp) {
    graphx::TcpTransportConfig transport;
    transport.host = "127.0.0.1";
    transport.bind = "127.0.0.1";
    transport.port = static_cast<std::uint16_t>(43000 + (::getpid() % 1000));
    edge.transport = std::move(transport);
  } else {
    graphx::UnixSocketTransportConfig transport;
    transport.path = "/tmp/graphx-factory-" + std::to_string(::getpid()) + ".sock";
    edge.transport = std::move(transport);
  }
  std::exception_ptr listener_error;
  std::thread listener([&] {
    try {
      auto receiver = factory.create(edge, graphx::ConnectionMode::listen);
      auto message = receiver->receive(2s);
      expect(message && message->payload == "request", "factory socket receive");
      message->payload = "response";
      receiver->send(*message);
    } catch (...) {
      listener_error = std::current_exception();
    }
  });
  try {
    graphx::TransportPtr sender;
    for (int attempt = 0; attempt < 40 && !sender; ++attempt) {
      try {
        sender = factory.create(edge, graphx::ConnectionMode::connect);
      } catch (const std::exception&) {
        std::this_thread::sleep_for(25ms);
      }
    }
    expect(static_cast<bool>(sender), "factory listener readiness");
    sender->send(graphx::Envelope::make(5, "Test", "request"));
    const auto reply = sender->receive(2s);
    expect(reply && reply->payload == "response", "factory socket reply");
  } catch (...) {
    if (listener.joinable()) listener.join();
    throw;
  }
  listener.join();
  if (listener_error) std::rethrow_exception(listener_error);
}

void tcp_factory_round_trip() { socket_factory_round_trip(graphx::TransportKind::tcp); }

void unix_factory_round_trip() { socket_factory_round_trip(graphx::TransportKind::unix_socket); }

void shared_memory_factory_round_trip() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "factory-shared";
  graphx::SharedMemoryTransportConfig transport;
  transport.segment = "/gx-factory-shared-" + std::to_string(::getpid());
  transport.capacity = 2;
  transport.max_message_bytes = 4096;
  edge.transport = std::move(transport);
  auto receiver = factory.create(edge, graphx::ConnectionMode::listen);
  auto sender = factory.create(edge, graphx::ConnectionMode::connect);
  sender->send(graphx::Envelope::make(6, "Test", "shared factory"));
  const auto message = receiver->receive(100ms);
  expect(message && message->sequence == 6 && message->payload == "shared factory",
         "shared-memory factory delivery");
}

void shared_memory_factory_uses_connect_timeout() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "factory-shared-timeout";
  graphx::SharedMemoryTransportConfig transport;
  transport.segment = "/gx-factory-missing-" + std::to_string(::getpid());
  transport.connect_timeout_ms = 30;
  edge.transport = std::move(transport);
  const auto start = std::chrono::steady_clock::now();
  bool failed{};
  try {
    [[maybe_unused]] auto ignored = factory.create(edge, graphx::ConnectionMode::connect);
  } catch (const std::exception&) {
    failed = true;
  }
  expect(failed, "missing shared segment connected");
  expect(std::chrono::steady_clock::now() - start < 1s,
         "shared-memory factory connect timeout propagation");
}

void udp_factory_round_trip() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "factory-udp";
  graphx::UdpTransportConfig transport;
  transport.mode = graphx::UdpMode::unicast;
  transport.destination = "127.0.0.1";
  transport.bind = "127.0.0.1";
  transport.port = static_cast<std::uint16_t>(45000 + (::getpid() % 1000));
  transport.receive_buffer_bytes = 65536;
  transport.send_buffer_bytes = 65536;
  transport.max_datagram_bytes = 1400;
  edge.transport = std::move(transport);
  auto receiver = factory.create(edge, graphx::ConnectionMode::listen);
  auto sender = factory.create(edge, graphx::ConnectionMode::connect);
  sender->send(graphx::Envelope::make(7, "Test", "udp factory"));
  const auto message = receiver->receive(500ms);
  expect(message && message->sequence == 7 && message->payload == "udp factory",
         "UDP factory delivery");
}
}  // namespace
int main() {
  return run_tests(
      {{"resolved settings", resolved_transport_settings},
       {"in_process_factory_shares_named_channel", in_process_factory_shares_named_channel},
       {"factory_rejects_unvalidated_settings", factory_rejects_unvalidated_settings},
       {"tcp_factory_round_trip", tcp_factory_round_trip},
       {"unix_factory_round_trip", unix_factory_round_trip},
       {"shared_memory_factory_round_trip", shared_memory_factory_round_trip},
       {"shared_memory_factory_uses_connect_timeout", shared_memory_factory_uses_connect_timeout},
       {"udp_factory_round_trip", udp_factory_round_trip}});
}
