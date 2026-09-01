#include "graphx/config.hpp"
#include "graphx/infra.hpp"
#include "graphx/transport_factory.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class TemporaryConfig {
 public:
  explicit TemporaryConfig(std::string_view contents)
      : path_(std::filesystem::temp_directory_path() /
              ("graphx-config-" + std::to_string(::getpid()) + "-" + std::to_string(counter_++) +
               ".yaml")) {
    std::ofstream output(path_);
    output << contents;
    if (!output) throw std::runtime_error("could not write temporary configuration");
  }
  ~TemporaryConfig() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  inline static unsigned counter_{};
};

constexpr std::string_view valid_config = R"yaml(
version: 1
graph:
  id: test-graph
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: Sample }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: Sample }]
  edges:
    - { id: sample-edge, from: source.out, to: target.in, transport: tcp }
transport:
  tcp:
    sample-edge:
      host: target
      bind: 0.0.0.0
      port: 7001
      framing: u32be
      connect_timeout_ms: 1200
      send_timeout_ms: 900
      reconnect: true
      retry: { max_attempts: 7, initial_backoff_ms: 10, max_backoff_ms: 80 }
)yaml";

bool diagnostic_contains(const graphx::ConfigError& error, std::string_view text) {
  for (const auto& diagnostic : error.diagnostics())
    if (diagnostic.path.find(text) != std::string::npos ||
        diagnostic.message.find(text) != std::string::npos)
      return true;
  return false;
}

void authoritative_config_loads() {
  const auto config = graphx::load_config(std::filesystem::path(GRAPHX_SOURCE_DIR) / "graphx.yaml");
  expect(config.version == 1 && config.id == "sample-pipeline", "root model");
  expect(config.nodes.size() == 3 && config.edges.size() == 2, "topology counts");
  expect(config.edge("samples").transport.host == "transform", "TCP settings");
  expect(config.node("transform").ports.size() == 2, "node lookup");
  expect(config.deployment.services.size() == 3, "deployment placements");
  expect(config.deployment.services.front().node_id == "generator", "deployment separation");
  expect(config.network_infrastructure.networks.size() == 1, "network layer");
  expect(config.network_infrastructure.network("graphx").driver == graphx::NetworkDriver::bridge,
         "bridge network model");
  expect(config.network_infrastructure.edge_path("samples").hops.size() == 3,
         "simple network path");
  expect(config.observability.metrics.enabled && config.observability.metrics.exporters.size() == 2,
         "typed metrics configuration");
  expect(config.observability.telemetry.host == "telemetry" &&
             config.observability.telemetry.heartbeat_timeout_ms == 5000,
         "typed telemetry configuration");
}

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
version: 1
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

void invalid_observability_is_rejected() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  metrics: { enabled: true, exporters: [console, console, mystery] }
  telemetry: { host: collector, port: 9000, websocket: ws, heartbeat_interval_ms: 1000, heartbeat_timeout_ms: 1500 }
  capture: { enabled: true }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid observability configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "duplicate exporter"), "duplicate exporter diagnostic");
    expect(diagnostic_contains(error, "must be 'console'"), "unknown exporter diagnostic");
    expect(diagnostic_contains(error, "must start with"), "WebSocket path diagnostic");
    expect(diagnostic_contains(error, "at least twice"), "heartbeat relationship diagnostic");
    expect(diagnostic_contains(error, "capture.provider"), "capture provider diagnostic");
  }
}

void capture_configuration() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  capture: { enabled: true, provider: pcapng, directory: test-captures }
)yaml");
  const auto capture = graphx::load_config(file.path()).observability.capture;
  expect(capture.enabled && capture.provider == "pcapng" && capture.directory == "test-captures",
         "PCAPNG capture configuration");
}

void invalid_shared_memory_config_is_rejected() {
  TemporaryConfig file(R"yaml(
version: 1
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

void mixed_network_model_and_plan_load() {
  const auto config = graphx::load_config(std::filesystem::path(GRAPHX_SOURCE_DIR) /
                                          "examples/mixed-network/graphx.yaml");
  expect(config.network_infrastructure.network("gx-mac-domain").driver ==
             graphx::NetworkDriver::macvlan,
         "macvlan model");
  expect(config.network_infrastructure.network("gx-ipv-domain").mode == "l2", "ipvlan L2 model");
  expect(config.network_infrastructure.router("domain-router").interfaces.size() == 2,
         "router interfaces");
  expect(config.network_infrastructure.network_switch("br-gx-mac").mirror.has_value(),
         "OVS mirror model");
  const auto commands = graphx::infrastructure_plan(config, graphx::InfraAction::create);
  std::string plan;
  for (const auto& command : commands) plan += graphx::format_command(command) + '\n';
  expect(plan.find("add-br br-gx-mac") != std::string::npos, "mac OVS bridge plan");
  expect(plan.find("--driver macvlan") != std::string::npos, "macvlan create plan");
  expect(plan.find("ipvlan_mode=l2") != std::string::npos, "ipvlan create plan");
  expect(plan.find("net.ipv4.ip_forward=1") != std::string::npos, "forwarding plan");
  expect(plan.find("create Mirror") != std::string::npos, "mirror plan");
  const auto fault =
      graphx::netem_command(config, "domain-router", "ipv", false, "20ms", "3ms", "1%", {});
  expect(graphx::format_command(fault).find("netem delay 20ms 3ms loss 1%") != std::string::npos,
         "netem plan");
}

void standalone_network_examples_load() {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples";
  const auto macvlan = graphx::load_config(root / "macvlan/graphx.yaml");
  expect(macvlan.network_infrastructure.networks.size() == 1, "standalone macvlan domain");
  expect(!macvlan.network_infrastructure.interfaces.front().mac.empty(),
         "standalone macvlan explicit MAC");

  const auto layer_two = graphx::load_config(root / "ipvlan-l2/graphx.yaml");
  expect(layer_two.network_infrastructure.networks.size() == 3, "one IPvlan L2 domain per node");
  expect(layer_two.network_infrastructure.routers.size() == 1 &&
             layer_two.network_infrastructure.switches.size() == 3,
         "IPvlan L2 routed domains");

  const auto layer_three = graphx::load_config(root / "ipvlan-l3/graphx.yaml");
  expect(layer_three.network_infrastructure.networks.size() == 1 &&
             layer_three.network_infrastructure.networks.front().subnets.size() == 3,
         "one supported multi-subnet IPvlan L3 network");
  std::string plan;
  for (const auto& command : graphx::infrastructure_plan(layer_three, graphx::InfraAction::create))
    plan += graphx::format_command(command) + '\n';
  expect(plan.find("ipvlan_mode=l3") != std::string::npos, "IPvlan L3 plan");
  expect(plan.find("--subnet 10.42.1.0/24 --subnet 10.42.2.0/24 --subnet 10.42.3.0/24") !=
             std::string::npos,
         "IPvlan L3 multi-subnet plan");
  expect(plan.find("--gateway") == std::string::npos, "IPvlan L3 omits gateway");
}

void invalid_network_reference_is_rejected() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
network:
  networks:
    - { id: lab, driver: bridge, subnet: 10.0.0.0/24, gateway: 10.0.0.1 }
  interfaces:
    source:
      - { id: data, network: missing, address: 10.0.0.10/24 }
  edge_paths:
    sample-edge: [source, missing, target]
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid network reference was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "unknown network 'missing'"), "unknown network diagnostic");
    expect(diagnostic_contains(error, "unknown hop 'missing'"), "unknown hop diagnostic");
  }
}

void explicit_override_wins() {
  TemporaryConfig file(valid_config);
  ::setenv("GRAPHX_OVERRIDES", "transport.tcp.sample-edge.host=environment", 1);
  const auto config = graphx::load_config(
      file.path(),
      {{"transport.tcp.sample-edge.host", "explicit"}, {"transport.tcp.sample-edge.port", "8123"}});
  ::unsetenv("GRAPHX_OVERRIDES");
  expect(config.edge("sample-edge").transport.host == "explicit", "override precedence");
  expect(config.edge("sample-edge").transport.port == 8123, "numeric override");
}

void invalid_override_is_rejected() {
  TemporaryConfig file(valid_config);
  try {
    [[maybe_unused]] const auto ignored =
        graphx::load_config(file.path(), {{"transport.tcp.typo.host", "value"}});
    throw std::runtime_error("unknown override was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "does not exist"), "override diagnostic");
  }
}

void semantic_errors_are_aggregated() {
  TemporaryConfig file(R"yaml(
version: 1
unexpected: true
graph:
  id: bad graph
  nodes:
    - id: source
      kind: source
      ports: [{ name: in, direction: input, schema: A }]
    - id: target
      kind: sink
      ports: [{ name: out, direction: output, schema: B }]
  edges:
    - { id: bad-edge, from: source.in, to: target.out, transport: tcp }
transport:
  tcp:
    bad-edge: { host: target, bind: 0.0.0.0, port: 70000, framing: other }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(error.diagnostics().size() >= 6, "aggregated diagnostic count");
    expect(diagnostic_contains(error, "unexpected"), "unknown-key diagnostic");
    expect(diagnostic_contains(error, "between 1 and 65535"), "port diagnostic");
    expect(diagnostic_contains(error, "source port"), "direction diagnostic");
    expect(diagnostic_contains(error, "schema mismatch"), "schema diagnostic");
  }
}

void cycle_is_rejected() {
  TemporaryConfig file(R"yaml(
version: 1
graph:
  id: cyclic
  nodes:
    - id: a
      kind: map
      ports: [{ name: in, direction: input, schema: S }, { name: out, direction: output, schema: S }]
    - id: b
      kind: map
      ports: [{ name: in, direction: input, schema: S }, { name: out, direction: output, schema: S }]
  edges:
    - { id: ab, from: a.out, to: b.in, transport: in_process }
    - { id: ba, from: b.out, to: a.in, transport: in_process }
transport:
  in_process:
    ab: { channel: ab }
    ba: { channel: ba }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("cycle was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "cycles are not supported"), "cycle diagnostic");
  }
}

void invalid_deployment_is_rejected() {
  TemporaryConfig file(R"yaml(
version: 1
graph:
  id: deployed
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
    local: { channel: local }
deployment:
  services:
    source: { image: graphx/source:latest, command: graphx-source }
    ghost: { image: graphx/ghost:latest, command: graphx-ghost }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid deployment was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "unknown graph node"), "unknown placement diagnostic");
    expect(diagnostic_contains(error, "missing placement for node 'target'"),
           "missing placement diagnostic");
  }
}

void malformed_and_oversized_files_are_rejected() {
  TemporaryConfig malformed("version: [\n");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(malformed.path());
    throw std::runtime_error("malformed YAML was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "invalid YAML"), "YAML diagnostic");
  }
  TemporaryConfig oversized(std::string(graphx::kMaxConfigBytes + 1, 'x'));
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(oversized.path());
    throw std::runtime_error("oversized configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "1 MiB"), "size diagnostic");
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
version: 1
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
version: 1
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
version: 1
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
version: 1
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
version: 1
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
version: 1
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

}  // namespace

int main() {
  ::unsetenv("GRAPHX_OVERRIDES");
  const std::pair<const char*, std::function<void()>> tests[] = {
      {"authoritative config", authoritative_config_loads},
      {"TCP policy", tcp_policy_loads},
      {"invalid TCP policy", invalid_tcp_policy_is_rejected},
      {"shared-memory config", shared_memory_config_loads},
      {"invalid observability", invalid_observability_is_rejected},
      {"capture config", capture_configuration},
      {"invalid shared-memory config", invalid_shared_memory_config_is_rejected},
      {"mixed network model", mixed_network_model_and_plan_load},
      {"standalone network examples", standalone_network_examples_load},
      {"invalid network reference", invalid_network_reference_is_rejected},
      {"override precedence", explicit_override_wins},
      {"invalid override", invalid_override_is_rejected},
      {"aggregated errors", semantic_errors_are_aggregated},
      {"cycle", cycle_is_rejected},
      {"invalid deployment", invalid_deployment_is_rejected},
      {"malformed and oversized", malformed_and_oversized_files_are_rejected},
      {"in-process queue config", in_process_queue_config_loads_and_validates},
      {"Unix socket deadline config", unix_socket_deadline_config_loads_and_validates},
      {"TCP TLS config", tcp_tls_config_loads_and_validates},
      {"in-process factory", in_process_factory_shares_named_channel},
      {"factory validation", factory_rejects_unvalidated_settings},
      {"TCP factory", tcp_factory_round_trip},
      {"Unix socket factory", unix_factory_round_trip},
      {"shared-memory factory", shared_memory_factory_round_trip},
      {"shared-memory factory timeout", shared_memory_factory_uses_connect_timeout}};
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
