#include "config_test_support.hpp"

using namespace std::chrono_literals;
using namespace config_test;

namespace {

void tcp_policy_loads() {
  TemporaryConfig file(valid_config);
  const auto transport = graphx::load_config(file.path()).edge("sample-edge").transport;
  expect(transport.connect_timeout_ms == 1200 && transport.send_timeout_ms == 900, "TCP timeouts");
  expect(transport.retry_attempts == 7 && transport.retry_initial_backoff_ms == 10 &&
             transport.retry_max_backoff_ms == 80,
         "TCP retry policy");
  expect(transport.reconnect, "TCP reconnect policy");
}

void invalid_tcp_policy_is_rejected() {
  auto source = std::string(valid_config);
  const auto marker = source.find("max_backoff_ms: 80");
  source.replace(marker, std::string("max_backoff_ms: 80").size(), "max_backoff_ms: 5");
  TemporaryConfig file(source);
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid TCP backoff was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "greater than or equal"), "TCP backoff diagnostic");
  }
}

void shared_memory_config_loads() {
  TemporaryConfig file(R"yaml(
version: 2
graph:
  id: shared-graph
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: Sample }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: Sample }]
  edges:
    - { id: shared-edge, from: source.out, to: target.in, transport: shared_memory }
transport:
  shared_memory:
    shared-edge:
      segment: gx-config-shared
      capacity: 8
      max_message_bytes: 8192
      backpressure: reject
      connect_timeout_ms: 75
      send_timeout_ms: 250
)yaml");
  const auto transport = graphx::load_config(file.path()).edge("shared-edge").transport;
  expect(transport.kind == graphx::TransportKind::shared_memory, "shared-memory kind");
  expect(transport.segment == "gx-config-shared" && transport.capacity == 8,
         "shared-memory layout settings");
  expect(transport.max_message_bytes == 8192 && transport.backpressure == "reject" &&
             transport.send_timeout_ms == 250 && transport.connect_timeout_ms == 75,
         "shared-memory pressure settings");
}

void invalid_shared_memory_config_is_rejected() {
  TemporaryConfig file(R"yaml(
version: 2
graph:
  id: shared-graph
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: Sample }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: Sample }]
  edges:
    - { id: shared-edge, from: source.out, to: target.in, transport: shared_memory }
transport:
  shared_memory:
    shared-edge: { segment: bad/name, capacity: 0, max_message_bytes: 32, backpressure: discard }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid shared-memory configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "segment"), "shared-memory segment diagnostic");
    expect(diagnostic_contains(error, "between 1 and 65536"), "shared-memory capacity diagnostic");
    expect(diagnostic_contains(error, "between 64"), "shared-memory size diagnostic");
    expect(diagnostic_contains(error, "block' or 'reject"), "shared-memory policy diagnostic");
  }
}

void in_process_factory_shares_named_channel() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "local";
  edge.transport.kind = graphx::TransportKind::in_process;
  edge.transport.channel = "local-channel";
  auto sender = factory.create(edge, graphx::ConnectionMode::connect);
  auto receiver = factory.create(edge, graphx::ConnectionMode::listen);
  sender->send(graphx::Envelope::make(4, "Test", "factory"));
  const auto message = receiver->receive(20ms);
  expect(message && message->payload == "factory", "factory in-process delivery");
  edge.transport.capacity = 2;
  try {
    [[maybe_unused]] auto inconsistent = factory.create(edge, graphx::ConnectionMode::listen);
    throw std::runtime_error("inconsistent named channel settings were accepted");
  } catch (const std::invalid_argument& error) {
    expect(std::string_view(error.what()).find("inconsistent") != std::string_view::npos,
           "factory rejects inconsistent in-process settings");
  }
}

void in_process_queue_config_loads_and_validates() {
  TemporaryConfig valid(R"yaml(
version: 2
graph:
  id: bounded-local
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: S }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: S }]
  edges:
    - { id: local, from: source.out, to: target.in, transport: in_process }
transport:
  in_process:
    local: { channel: bounded, capacity: 7, backpressure: reject, send_timeout_ms: 25 }
)yaml");
  const auto config = graphx::load_config(valid.path());
  const auto& settings = config.edge("local").transport;
  expect(
      settings.capacity == 7 && settings.backpressure == "reject" && settings.send_timeout_ms == 25,
      "bounded in-process settings load");

  TemporaryConfig invalid(R"yaml(
version: 2
graph:
  id: invalid-local
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: S }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: S }]
  edges:
    - { id: local, from: source.out, to: target.in, transport: in_process }
transport:
  in_process:
    local: { channel: bounded, capacity: 0, backpressure: drop, send_timeout_ms: 0 }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(invalid.path());
    throw std::runtime_error("invalid in-process queue settings were accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "between 1 and 65536") &&
               diagnostic_contains(error, "must be 'block' or 'reject'") &&
               diagnostic_contains(error, "between 1 and 600000"),
           "invalid in-process queue diagnostics");
  }
}

void unix_socket_deadline_config_loads_and_validates() {
  TemporaryConfig valid(R"yaml(
version: 2
graph:
  id: bounded-unix
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: S }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: S }]
  edges:
    - { id: local, from: source.out, to: target.in, transport: unix }
transport:
  unix:
    local: { path: /tmp/graphx-bounded.sock, connect_timeout_ms: 30, send_timeout_ms: 40 }
)yaml");
  const auto config = graphx::load_config(valid.path());
  const auto& settings = config.edge("local").transport;
  expect(settings.connect_timeout_ms == 30 && settings.send_timeout_ms == 40,
         "Unix-domain deadlines load");

  TemporaryConfig invalid(R"yaml(
version: 2
graph:
  id: invalid-unix
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: S }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: S }]
  edges:
    - { id: local, from: source.out, to: target.in, transport: unix }
transport:
  unix:
    local: { path: /tmp/graphx-invalid.sock, connect_timeout_ms: 0, send_timeout_ms: 0 }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(invalid.path());
    throw std::runtime_error("invalid Unix-domain deadlines were accepted");
  } catch (const graphx::ConfigError& error) {
    expect(error.diagnostics().size() >= 2 && diagnostic_contains(error, "between 1 and 600000"),
           "invalid Unix-domain deadline diagnostics");
  }
}

void tcp_tls_config_loads_and_validates() {
  TemporaryConfig valid(R"yaml(
version: 2
graph:
  id: secure-tcp
  nodes:
    - { id: source, kind: source, ports: [{ name: out, direction: output, schema: S }] }
    - { id: target, kind: sink, ports: [{ name: in, direction: input, schema: S }] }
  edges:
    - { id: secure, from: source.out, to: target.in, transport: tcp }
transport:
  tcp:
    secure:
      host: target
      bind: 0.0.0.0
      port: 7443
      tls:
        enabled: true
        verify_peer: true
        require_client_certificate: true
        ca_file: /run/secrets/graphx-ca.pem
        certificate_file: /run/secrets/graphx-peer.pem
        private_key_file: /run/secrets/graphx-peer.key
        server_name: target.internal
)yaml");
  const auto config = graphx::load_config(valid.path());
  const auto& tls = config.edge("secure").transport;
  expect(tls.tls_enabled && tls.tls_verify_peer && tls.tls_require_client_certificate &&
             tls.tls_ca_file == "/run/secrets/graphx-ca.pem" &&
             tls.tls_server_name == "target.internal",
         "TLS settings load");

  TemporaryConfig invalid(R"yaml(
version: 2
graph:
  id: invalid-tls
  nodes:
    - { id: source, kind: source, ports: [{ name: out, direction: output, schema: S }] }
    - { id: target, kind: sink, ports: [{ name: in, direction: input, schema: S }] }
  edges:
    - { id: secure, from: source.out, to: target.in, transport: tcp }
transport:
  tcp:
    secure:
      host: target
      bind: 0.0.0.0
      port: 7443
      tls: { enabled: true, require_client_certificate: true }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(invalid.path());
    throw std::runtime_error("incomplete TLS configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "certificate_file and private_key_file are required") &&
               diagnostic_contains(error, "client certificates are required"),
           "incomplete TLS diagnostics");
  }
}

std::string udp_config(std::string_view mode, std::string_view destination,
                       std::string_view extra = {}) {
  return "version: 2\n"
         "graph:\n"
         "  id: udp-test\n"
         "  nodes:\n"
         "    - { id: source, kind: source, ports: [{ name: out, direction: output, schema: "
         "Message }] }\n"
         "    - { id: target, kind: sink, ports: [{ name: in, direction: input, schema: Message }] "
         "}\n"
         "  edges:\n"
         "    - { id: datagrams, from: source.out, to: target.in, transport: udp }\n"
         "transport:\n"
         "  udp:\n"
         "    datagrams:\n"
         "      mode: " +
         std::string(mode) +
         "\n"
         "      destination: " +
         std::string(destination) +
         "\n"
         "      bind: 0.0.0.0\n"
         "      port: 47101\n"
         "      interface: 127.0.0.1\n"
         "      ttl: 1\n"
         "      loopback: true\n"
         "      reuse_address: true\n"
         "      receive_buffer_bytes: 65536\n"
         "      send_buffer_bytes: 65536\n"
         "      max_datagram_bytes: 1400\n"
         "      framing: u32be\n" +
         std::string(extra);
}

void udp_configuration_loads_and_validates() {
  for (const auto& [mode, destination] :
       {std::pair{"unicast", "127.0.0.1"}, std::pair{"broadcast", "127.255.255.255"},
        std::pair{"multicast", "239.255.42.1"}}) {
    TemporaryConfig file(udp_config(mode, destination));
    const auto config = graphx::load_config(file.path());
    const auto& udp = config.edge("datagrams").transport;
    expect(udp.kind == graphx::TransportKind::udp && udp.destination == destination &&
               udp.max_datagram_bytes == 1400 && udp.receive_buffer_bytes == 65536,
           "UDP configuration loads");
  }

  const auto changed = [](std::string source, std::string_view from, std::string_view to) {
    const auto position = source.find(from);
    if (position == std::string::npos) throw std::runtime_error("UDP fixture edit failed");
    source.replace(position, from.size(), to);
    return source;
  };
  const auto base = udp_config("unicast", "127.0.0.1");
  for (const auto& valid :
       {changed(base, "      port: 47101\n", "      port: 1\n"),
        changed(base, "      port: 47101\n", "      port: 65535\n"),
        changed(base, "      ttl: 1\n", "      ttl: 0\n"),
        changed(base, "      ttl: 1\n", "      ttl: 255\n"),
        changed(base, "      interface: 127.0.0.1\n", ""),
        changed(base, "      interface: 127.0.0.1\n", "      interface: \"\"\n"),
        changed(base, "      receive_buffer_bytes: 65536\n", "      receive_buffer_bytes: 4096\n"),
        changed(base, "      send_buffer_bytes: 65536\n", "      send_buffer_bytes: 268435456\n"),
        changed(base, "      max_datagram_bytes: 1400\n", "      max_datagram_bytes: 64\n"),
        changed(base, "      max_datagram_bytes: 1400\n", "      max_datagram_bytes: 65507\n")}) {
    TemporaryConfig file(valid);
    expect(graphx::load_config(file.path()).edge("datagrams").transport.kind ==
               graphx::TransportKind::udp,
           "UDP boundary configuration loads");
  }
  const std::pair<std::string, std::string> invalid[] = {
      {changed(base, "      mode: unicast\n", ""), ".mode"},
      {changed(base, "      destination: 127.0.0.1\n", ""), ".destination"},
      {changed(base, "      bind: 0.0.0.0\n", ""), ".bind"},
      {changed(base, "      port: 47101\n", ""), ".port"},
      {udp_config("stream", "127.0.0.1"), ".mode"},
      {udp_config("multicast", "127.0.0.1"), ".destination"},
      {udp_config("unicast", "239.255.42.1"), ".destination"},
      {udp_config("unicast", "255.255.255.255"), ".destination"},
      {udp_config("broadcast", "239.255.42.1"), ".destination"},
      {changed(base, "      destination: 127.0.0.1\n", "      destination: invalid\n"),
       ".destination"},
      {changed(base, "      destination: 127.0.0.1\n", "      destination: ::1\n"), ".destination"},
      {changed(base, "      bind: 0.0.0.0\n", "      bind: invalid\n"), ".bind"},
      {changed(base, "      port: 47101\n", "      port: 0\n"), ".port"},
      {changed(base, "      port: 47101\n", "      port: 65536\n"), ".port"},
      {changed(base, "      port: 47101\n", "      port: \"47101\"\n"), ".port"},
      {changed(base, "      interface: 127.0.0.1\n", "      interface: bad/interface\n"),
       ".interface"},
      {changed(base, "      interface: 127.0.0.1\n", "      interface: [lo]\n"), ".interface"},
      {changed(base, "      ttl: 1\n", "      ttl: 256\n"), ".ttl"},
      {changed(base, "      ttl: 1\n", "      ttl: 4294967296\n"), ".ttl"},
      {changed(base, "      loopback: true\n", "      loopback: \"true\"\n"), ".loopback"},
      {changed(base, "      reuse_address: true\n", "      reuse_address: 1\n"), ".reuse_address"},
      {changed(base, "      receive_buffer_bytes: 65536\n", "      receive_buffer_bytes: 4095\n"),
       ".receive_buffer_bytes"},
      {changed(base, "      receive_buffer_bytes: 65536\n",
               "      receive_buffer_bytes: 4294967296\n"),
       ".receive_buffer_bytes"},
      {changed(base, "      send_buffer_bytes: 65536\n", "      send_buffer_bytes: 268435457\n"),
       ".send_buffer_bytes"},
      {udp_config("unicast", "127.0.0.1", "      reconnect: true\n"), "reconnect"},
      {udp_config("unicast", "127.0.0.1", "      tls: { enabled: true }\n"), "tls"},
      {udp_config("unicast", "127.0.0.1", "      unknown: true\n"), "unknown"},
      {udp_config("unicast", "127.0.0.1", "    orphan:\n      mode: unicast\n"),
       "transport.udp.orphan"},
      {udp_config("unicast", "127.0.0.1", "      max_datagram_bytes: 63\n"), "max_datagram_bytes"},
      {udp_config("unicast", "127.0.0.1", "      max_datagram_bytes: 65508\n"),
       "max_datagram_bytes"},
      {udp_config("unicast", "127.0.0.1", "      framing: raw\n"), ".framing"}};
  for (const auto& [source, expected] : invalid) {
    TemporaryConfig file(source);
    try {
      [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
      throw std::runtime_error("invalid UDP configuration was accepted");
    } catch (const graphx::ConfigError& error) {
      expect(diagnostic_contains(error, expected), "UDP diagnostic contains precise path");
    }
  }
}

void factory_rejects_unvalidated_settings() {
  graphx::TransportFactory factory;
  graphx::EdgeConfig edge;
  edge.edge.id = "invalid";
  edge.transport.kind = graphx::TransportKind::tcp;
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
  edge.transport.kind = kind;
  if (kind == graphx::TransportKind::tcp) {
    edge.transport.host = "127.0.0.1";
    edge.transport.bind = "127.0.0.1";
    edge.transport.port = static_cast<std::uint16_t>(43000 + (::getpid() % 1000));
  } else {
    edge.transport.path = "/tmp/graphx-factory-" + std::to_string(::getpid()) + ".sock";
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
  edge.transport.kind = graphx::TransportKind::shared_memory;
  edge.transport.segment = "/gx-factory-shared-" + std::to_string(::getpid());
  edge.transport.capacity = 2;
  edge.transport.max_message_bytes = 4096;
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
  edge.transport.kind = graphx::TransportKind::shared_memory;
  edge.transport.segment = "/gx-factory-missing-" + std::to_string(::getpid());
  edge.transport.connect_timeout_ms = 30;
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
  edge.transport.kind = graphx::TransportKind::udp;
  edge.transport.udp_mode = graphx::UdpMode::unicast;
  edge.transport.destination = "127.0.0.1";
  edge.transport.bind = "127.0.0.1";
  edge.transport.port = static_cast<std::uint16_t>(45000 + (::getpid() % 1000));
  edge.transport.receive_buffer_bytes = 65536;
  edge.transport.send_buffer_bytes = 65536;
  edge.transport.max_datagram_bytes = 1400;
  auto receiver = factory.create(edge, graphx::ConnectionMode::listen);
  auto sender = factory.create(edge, graphx::ConnectionMode::connect);
  sender->send(graphx::Envelope::make(7, "Test", "udp factory"));
  const auto message = receiver->receive(500ms);
  expect(message && message->sequence == 7 && message->payload == "udp factory",
         "UDP factory delivery");
}

}  // namespace

int main() {
  return run_tests({
      {"TCP policy", tcp_policy_loads},
      {"invalid TCP policy", invalid_tcp_policy_is_rejected},
      {"shared-memory config", shared_memory_config_loads},
      {"invalid shared-memory config", invalid_shared_memory_config_is_rejected},
      {"in-process queue config", in_process_queue_config_loads_and_validates},
      {"Unix socket deadline config", unix_socket_deadline_config_loads_and_validates},
      {"TCP TLS config", tcp_tls_config_loads_and_validates},
      {"UDP config", udp_configuration_loads_and_validates},
      {"in-process factory", in_process_factory_shares_named_channel},
      {"factory validation", factory_rejects_unvalidated_settings},
      {"TCP factory", tcp_factory_round_trip},
      {"Unix socket factory", unix_factory_round_trip},
      {"shared-memory factory", shared_memory_factory_round_trip},
      {"shared-memory factory timeout", shared_memory_factory_uses_connect_timeout},
      {"UDP factory", udp_factory_round_trip},
  });
}
