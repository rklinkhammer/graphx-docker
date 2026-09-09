#include "graphx/config.hpp"
#include "graphx/infra.hpp"
#include "graphx/migration.hpp"
#include "graphx/transport_factory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
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

std::string valid_v2_config() {
  auto source = std::string(valid_config);
  source.replace(source.find("version: 1"), std::string("version: 1").size(), "version: 2");
  source += R"yaml(
network:
  networks:
    - id: semantic-lan
      profile: ethernet
      subnets: [10.80.0.0/24]
      gateway: 10.80.0.1
      external: false
  attachments:
    - { id: source-data, kind: external, owner: source, network: semantic-lan, address: 10.80.0.10/24, mac: "02:80:00:00:00:10" }
    - { id: target-data, kind: external, owner: target, network: semantic-lan, address: 10.80.0.20/24 }
)yaml";
  return source;
}

void version_two_profiles_and_attachments_load() {
  TemporaryConfig file(valid_v2_config());
  const auto config = graphx::load_config(file.path());
  expect(config.version == 2, "version 2 model");
  const auto& network = config.network_infrastructure.network("semantic-lan");
  expect(network.profile == graphx::NetworkProfile::ethernet && network.parent.empty(),
         "semantic profile model");
  expect(config.network_infrastructure.interfaces.empty() &&
             config.network_infrastructure.attachments.size() == 2 &&
             config.network_infrastructure.attachments.front().kind ==
                 graphx::AttachmentKind::external,
         "version 2 attachment model");

  const struct ProfileCase {
    graphx::NetworkProfile profile{graphx::NetworkProfile::ethernet};
    std::string_view name;
    std::string_view mac_identity;
    std::string_view learning;
    std::string_view filtering;
    std::string_view arp;
    std::string_view broadcast;
    std::string_view multicast;
    std::string_view routing;
    std::string_view isolation;
    std::string_view management;
  } profiles[] = {
      {graphx::NetworkProfile::ethernet, "ethernet", "endpoint", "dynamic", "ovs", "endpoint",
       "flood", "flood", "l2", "none", "separate"},
      {graphx::NetworkProfile::macvlan, "macvlan", "endpoint", "dynamic", "ovs", "endpoint",
       "flood", "flood", "l2", "host", "separate"},
      {graphx::NetworkProfile::ipvlan_l2, "ipvlan-l2", "shared-uplink", "suppressed", "ovs",
       "endpoint-shared-mac", "flood", "flood", "l2", "host", "separate"},
      {graphx::NetworkProfile::ipvlan_l3, "ipvlan-l3", "shared-uplink", "none", "route",
       "suppressed", "suppressed", "suppressed", "l3", "endpoint", "separate"},
      {graphx::NetworkProfile::ipvlan_l3s, "ipvlan-l3s", "shared-uplink", "none",
       "source-validated", "suppressed", "suppressed", "suppressed", "l3-source-validated",
       "endpoint", "separate"},
  };
  for (const auto& profile : profiles) {
    expect(graphx::to_string(profile.profile) == profile.name, "profile name");
    const auto& semantics = graphx::profile_semantics(profile.profile);
    expect(semantics.mac_identity == profile.mac_identity &&
               semantics.learning == profile.learning && semantics.filtering == profile.filtering &&
               semantics.arp == profile.arp && semantics.broadcast == profile.broadcast &&
               semantics.multicast == profile.multicast && semantics.routing == profile.routing &&
               semantics.isolation == profile.isolation &&
               semantics.management == profile.management,
           "exact profile behavior");
  }
}

void version_two_is_strict_and_not_realized_by_v1_planner() {
  const std::pair<std::string, std::string> invalid[] = {
      {"version: 2", "version: \"2\""},
      {"profile: ethernet", "driver: bridge"},
      {"profile: ethernet", "profile: mystery"},
      {"profile: ethernet", "profile: macvlan"},
      {"external: false", "external: \"false\""},
      {"subnets: [10.80.0.0/24]", "subnets: [10.80.0.0/24, 10.81.0.0/24]"},
  };
  for (const auto& [from, to] : invalid) {
    auto source = valid_v2_config();
    source.replace(source.find(from), from.size(), to);
    TemporaryConfig file(source);
    try {
      [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
      throw std::runtime_error("invalid version 2 configuration was accepted");
    } catch (const graphx::ConfigError&) {
    }
  }

  TemporaryConfig file(valid_v2_config());
  const auto config = graphx::load_config(file.path());
  try {
    [[maybe_unused]] const auto ignored =
        graphx::infrastructure_plan(config, graphx::InfraAction::create);
    throw std::runtime_error("version 2 used the legacy infrastructure planner");
  } catch (const std::invalid_argument& error) {
    expect(std::string_view(error.what()).find("migration M3") != std::string_view::npos,
           "version 2 planner boundary diagnostic");
  }
}

void version_one_migration_is_deterministic() {
  const auto source =
      std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples/mixed-network/graphx.yaml";
  const auto first = graphx::migrate_config_v1_to_v2(source);
  const auto second = graphx::migrate_config_v1_to_v2(source);
  expect(first == second, "migration byte determinism");
  ::setenv("GRAPHX_OVERRIDES", "version=2", 1);
  try {
    const auto overridden_environment = graphx::migrate_config_v1_to_v2(source);
    ::unsetenv("GRAPHX_OVERRIDES");
    expect(overridden_environment == first, "migration ignores ambient overrides");
  } catch (...) {
    ::unsetenv("GRAPHX_OVERRIDES");
    throw;
  }
  expect(first.find("version: 2") != std::string::npos &&
             first.find("profile: macvlan") != std::string::npos &&
             first.find("profile: ipvlan-l2") != std::string::npos &&
             first.find("kind: container_veth") != std::string::npos &&
             first.find("kind: namespace_veth") != std::string::npos &&
             first.find("kind: mirror") != std::string::npos &&
             first.find("driver:") == std::string::npos &&
             first.find("\n  interfaces:") == std::string::npos,
         "reviewable migration mapping");
  TemporaryConfig migrated(first);
  const auto config = graphx::load_config(migrated.path());
  expect(config.version == 2 && config.network_infrastructure.attachments.size() == 7,
         "migrated version 2 validates");

  const auto expect_invalid_attachment = [&](std::string candidate, std::string_view marker,
                                             std::string_view from, std::string_view to,
                                             std::string_view diagnostic) {
    const auto marker_position = candidate.find(marker);
    expect(marker_position != std::string::npos, "attachment test marker");
    const auto position = candidate.find(from, marker_position);
    expect(position != std::string::npos, "attachment test field");
    candidate.replace(position, from.size(), to);
    TemporaryConfig invalid(candidate);
    try {
      [[maybe_unused]] const auto ignored = graphx::load_config(invalid.path());
      throw std::runtime_error("contradictory attachment was accepted");
    } catch (const graphx::ConfigError& error) {
      expect(diagnostic_contains(error, diagnostic), "attachment mismatch diagnostic");
    }
  };
  constexpr std::string_view namespace_marker = "kind: namespace_veth";
  expect_invalid_attachment(first, namespace_marker, "network: gx-mac-domain",
                            "network: gx-ipv-domain", "exactly match");
  expect_invalid_attachment(first, namespace_marker, "address: 10.10.0.1/24",
                            "address: 10.10.0.2/24", "exactly match");
  expect_invalid_attachment(first, namespace_marker, "interface: r-mac", "interface: r-other",
                            "exactly match");
  expect_invalid_attachment(first, namespace_marker, "peer: ovs-r-mac", "peer: ovs-other",
                            "exactly match");
  expect_invalid_attachment(first, namespace_marker, "switch: br-gx-mac", "switch: br-gx-ipv",
                            "exactly match");
  expect_invalid_attachment(first, namespace_marker, "switch: br-gx-mac", "switch: nonexistent",
                            "unknown switch");
  constexpr std::string_view mirror_marker = "kind: mirror";
  expect_invalid_attachment(first, mirror_marker, "interface: cap-mac-ovs", "interface: mv-ovs",
                            "mirror output-port");
  expect_invalid_attachment(first, "id: mirror-mac\n      kind: mirror", "id: mirror-mac",
                            "id: mirror-other", "configured on its OVS switch");

  const auto qemu_source =
      std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples/qemu-node/graphx.yaml";
  TemporaryConfig qemu(graphx::migrate_config_v1_to_v2(qemu_source));
  const auto qemu_config = graphx::load_config(qemu.path());
  expect(std::ranges::any_of(qemu_config.network_infrastructure.attachments,
                             [](const auto& item) {
                               return item.kind == graphx::AttachmentKind::qemu_tap &&
                                      item.owner == "qemu-node";
                             }),
         "QEMU attachment migration");
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
  expect(!config.observability.otlp.enabled && config.observability.otlp.queue_capacity == 1024 &&
             config.observability.otlp.retry_max_attempts == 3 &&
             config.observability.otlp.retry_initial_backoff_ms == 200 &&
             config.observability.otlp.retry_max_backoff_ms == 5000,
         "typed bounded OTLP configuration");
  expect(config.observability.slos.window_seconds == 300 &&
             config.observability.slos.availability_target == 0.99,
         "typed SLO configuration");
  expect(!config.observability.history.enabled &&
             config.observability.history.backend == "sqlite" &&
             config.observability.history.max_records == 100000 &&
             config.observability.history.max_database_bytes == 268435456,
         "typed bounded history configuration");
  expect(config.observability.control.command_timeout_ms == 2000 &&
             config.observability.control.max_commands == 1024 &&
             config.observability.control.max_request_bytes == 4096,
         "typed bounded control configuration");
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
  capture: { enabled: true, provider: pcapng, directory: test-captures, snaplen: 4096, max_file_bytes: 1048576, max_packets: 500 }
)yaml");
  const auto capture = graphx::load_config(file.path()).observability.capture;
  expect(capture.enabled && capture.provider == "pcapng" && capture.directory == "test-captures" &&
             capture.snaplen == 4096 && capture.max_file_bytes == 1048576 &&
             capture.max_packets == 500,
         "PCAPNG capture configuration");

  TemporaryConfig maximum(std::string(valid_config) + R"yaml(
observability:
  capture: { enabled: true, provider: pcapng, directory: captures, max_file_bytes: 4294967296 }
)yaml");
  expect(graphx::load_config(maximum.path()).observability.capture.max_file_bytes ==
             4ULL * 1024 * 1024 * 1024,
         "PCAPNG capture accepts its documented 64-bit byte limit");

  TemporaryConfig external_provider(std::string(valid_config) + R"yaml(
observability:
  capture: { enabled: true, provider: ovs-span }
)yaml");
  expect(graphx::load_config(external_provider.path()).observability.capture.provider == "ovs-span",
         "external capture provider does not require a PCAPNG directory");

  TemporaryConfig missing_directory(std::string(valid_config) + R"yaml(
observability:
  capture: { enabled: true, provider: pcapng }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(missing_directory.path());
    throw std::runtime_error("PCAPNG capture without a directory was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "capture.directory"), "capture directory diagnostic");
  }

  TemporaryConfig invalid(std::string(valid_config) + R"yaml(
observability:
  capture: { enabled: true, provider: pcapng, directory: captures, snaplen: 0, max_file_bytes: 10, max_packets: 0 }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(invalid.path());
    throw std::runtime_error("invalid capture limits were accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "capture.snaplen"), "capture snaplen diagnostic");
    expect(diagnostic_contains(error, "capture.max_file_bytes"), "capture byte limit diagnostic");
    expect(diagnostic_contains(error, "capture.max_packets"), "capture packet limit diagnostic");
  }

  const std::pair<std::string_view, std::string_view> invalid_types[] = {
      {"enabled: \"false\", provider: pcapng", "boolean, not a string"},
      {"enabled: true, provider: pcapng, directory: captures, snaplen: \"4096\"",
       "integer, not a string"},
      {"enabled: true, provider: pcapng, directory: captures, max_file_bytes: \"1048576\"",
       "integer, not a string"},
      {"enabled: true, provider: pcapng, directory: captures, max_packets: \"500\"",
       "integer, not a string"},
      {"enabled: true, provider: pcapgn, directory: captures", "must be 'pcapng' or 'ovs-span'"},
  };
  for (const auto& [capture, expected] : invalid_types) {
    TemporaryConfig invalid_type(std::string(valid_config) + "\nobservability:\n  capture: { " +
                                 std::string(capture) + " }\n");
    try {
      [[maybe_unused]] const auto ignored = graphx::load_config(invalid_type.path());
      throw std::runtime_error("invalid capture scalar or provider was accepted");
    } catch (const graphx::ConfigError& error) {
      expect(diagnostic_contains(error, expected), "strict capture configuration diagnostic");
    }
  }
}

void invalid_operations_configuration_is_rejected() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  otlp: { enabled: true, endpoint: "http://user@collector.example/path", traces_path: traces, export_interval_ms: 1, timeout_ms: 1, queue_capacity: 0, max_queue_bytes: 10, max_response_bytes: 100, retry_max_attempts: 0, retry_initial_backoff_ms: 100, retry_max_backoff_ms: 10 }
  slos: { window_seconds: 9, minimum_window_seconds: 20, availability_target: 1.1, max_error_ratio: -0.1, max_drop_ratio: 2, max_p95_latency_us: 0 }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid operations configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "otlp.traces_path"), "OTLP path diagnostic");
    expect(diagnostic_contains(error, "otlp.endpoint"), "OTLP endpoint diagnostic");
    expect(diagnostic_contains(error, "otlp.queue_capacity"), "OTLP queue diagnostic");
    expect(diagnostic_contains(error, "otlp.retry_max_attempts"), "OTLP retry count diagnostic");
    expect(diagnostic_contains(error, "otlp.retry_max_backoff_ms"),
           "OTLP retry backoff diagnostic");
    expect(diagnostic_contains(error, "slos.window_seconds"), "SLO window diagnostic");
    expect(diagnostic_contains(error, "slos.availability_target"), "SLO ratio diagnostic");
  }
}

void invalid_history_configuration_is_rejected() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  history: { enabled: true, backend: memory, database_file: history.sqlite, retention_seconds: 1, max_records: 1, max_database_bytes: 100, queue_capacity: 2, max_queue_bytes: 10, batch_size: 3, flush_interval_ms: 1, query_limit: 0, query_timeout_ms: 1, max_pending_queries: 0, shutdown_timeout_ms: 1 }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid history configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "history.backend"), "history backend diagnostic");
    expect(diagnostic_contains(error, "history.retention_seconds"), "history retention diagnostic");
    expect(diagnostic_contains(error, "history.max_database_bytes"), "history size diagnostic");
    expect(diagnostic_contains(error, "must not exceed queue_capacity"),
           "history batch relationship diagnostic");
    expect(diagnostic_contains(error, "history.query_timeout_ms"), "history query diagnostic");
  }
}

void history_scalar_types_are_strict() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  history:
    enabled: "false"
    query_limit: "200"
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("string history scalars were accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "history.enabled") &&
               diagnostic_contains(error, "boolean, not a string"),
           "history boolean type diagnostic");
    expect(diagnostic_contains(error, "history.query_limit") &&
               diagnostic_contains(error, "integer, not a string"),
           "history integer type diagnostic");
  }
}

void history_empty_and_unknown_values_are_strict() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  history:
    enabled: true
    backend: ""
    database_file: ""
    typo_retention_seconds: 60
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("empty and unknown history properties were accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "history.backend") &&
               diagnostic_contains(error, "must not be empty"),
           "empty history backend diagnostic");
    expect(diagnostic_contains(error, "history.database_file") &&
               diagnostic_contains(error, "must not be empty"),
           "empty history database diagnostic");
    expect(diagnostic_contains(error, "history.typo_retention_seconds") &&
               diagnostic_contains(error, "unknown property"),
           "unknown history property diagnostic");
  }
}

void invalid_control_configuration_is_rejected() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
observability:
  control: { command_timeout_ms: 99, command_retention_seconds: 1, max_commands: 0, max_audit_records: 1, idempotency_ttl_seconds: "60", max_request_bytes: 100, typo: 1 }
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid control configuration was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "control.command_timeout_ms"), "control timeout diagnostic");
    expect(diagnostic_contains(error, "control.max_commands"), "control capacity diagnostic");
    expect(diagnostic_contains(error, "control.idempotency_ttl_seconds") &&
               diagnostic_contains(error, "integer, not a string"),
           "control scalar type diagnostic");
    expect(diagnostic_contains(error, "control.typo") &&
               diagnostic_contains(error, "unknown property"),
           "control unknown property diagnostic");
  }
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

void static_route_policy_model_and_plan_load() {
  const auto path =
      std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples/static-route-policy/graphx.yaml";
  const auto config = graphx::load_config(path);
  const auto& router = config.network_infrastructure.router("route-router");
  expect(config.network_infrastructure.networks.size() == 3, "route lab domains");
  expect(config.network_infrastructure.switches.size() == 3, "route lab OVS switches");
  expect(router.interfaces.size() == 3 && router.routes.size() == 1 && router.policies.size() == 3,
         "route lab router model");
  expect(!router.routes.front().install_on_create, "manual route model");
  expect(config.network_infrastructure.edge_paths.size() == 3, "route lab ordered edge paths");
  std::string create_plan;
  std::string create_input;
  for (const auto& command : graphx::infrastructure_plan(config, graphx::InfraAction::create)) {
    create_plan += graphx::format_command(command) + '\n';
    create_input += command.standard_input;
  }
  expect(create_plan.find("add-br br-route-left") != std::string::npos, "route lab OVS plan");
  expect(create_plan.find("10.64.30.10/32") == std::string::npos,
         "manual route absent from create plan");
  expect(create_input.find("deny-middle-left") != std::string::npos &&
             create_input.find("counter drop") != std::string::npos,
         "ordered deny policy plan");
  const auto apply = graphx::route_command(config, "route-router", "10.64.30.10/32", false);
  expect(graphx::format_command(apply) ==
             "ip netns exec gx-route-router ip route replace 10.64.30.10/32 via 10.64.3.10 dev "
             "rt-right",
         "manual route apply command");
  const auto clear = graphx::route_command(config, "route-router", "10.64.30.10/32", true);
  expect(clear.ignore_failure &&
             graphx::format_command(clear).find("route delete 10.64.30.10/32") != std::string::npos,
         "manual route clear command");

  const auto transactional = graphx::infrastructure_plan(config, graphx::InfraAction::create, true);
  std::string transactional_plan;
  for (const auto& command : transactional) {
    transactional_plan += graphx::format_command(command) + '\n';
    if (!command.rollback_arguments.empty()) {
      expect(command.rollback_arguments.front() != "", "transaction rollback command");
      expect(!command.rollback_identity_arguments.empty(), "transaction rollback identity");
    }
  }
  expect(transactional_plan.find("ovs-vsctl add-br br-route-left") != std::string::npos &&
             transactional_plan.find("--may-exist add-br br-route-left") == std::string::npos,
         "transactional route lab uses strict bridge creation");
}

std::vector<std::string> file_identity_command(const std::filesystem::path& path) {
  return {"python3", "-c",
          "import os,sys; value=os.stat(sys.argv[1]); "
          "print(f'{value.st_ino}:{value.st_mode}')",
          path.string()};
}

void infrastructure_transaction_rolls_back_in_reverse() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("graphx-infra-transaction-" + std::to_string(::getpid()));
  const auto first = directory / "first";
  const auto second = directory / "second";
  std::filesystem::create_directories(directory);
  const std::vector<graphx::InfraCommand> commands = {
      {{"touch", first.string()}, {}, false, {"rm", first.string()}, file_identity_command(first)},
      {{"touch", second.string()},
       {},
       false,
       {"rm", second.string()},
       file_identity_command(second)},
      {{"false"}, {}, false, {}, {}},
  };
  std::ostringstream output, errors;
  const auto status = graphx::execute_infrastructure_plan(commands, false, output, errors);
  expect(status != 0, "transaction reports command failure");
  expect(!std::filesystem::exists(first) && !std::filesystem::exists(second),
         "transaction removes completed resources");
  const auto log = output.str();
  expect(log.find("- rm " + second.string()) < log.find("- rm " + first.string()),
         "transaction rolls back in reverse order");
  std::filesystem::remove_all(directory);
}

void infrastructure_transaction_preserves_replacement() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("graphx-infra-replacement-" + std::to_string(::getpid()));
  const auto resource = directory / "resource";
  std::filesystem::create_directories(directory);
  const std::vector<graphx::InfraCommand> commands = {
      {{"touch", resource.string()},
       {},
       false,
       {"rm", "-rf", resource.string()},
       file_identity_command(resource)},
      {{"sh", "-c",
        "rm '" + resource.string() + "' && mkdir '" + resource.string() + "' && exit 23"},
       {},
       false,
       {},
       {}},
  };
  std::ostringstream output, errors;
  const auto status = graphx::execute_infrastructure_plan(commands, false, output, errors);
  expect(status == 23, "replacement transaction reports injected failure");
  expect(std::filesystem::is_directory(resource), "transaction preserves replacement resource");
  expect(output.str().find("rollback skipped; resource identity changed") != std::string::npos,
         "transaction reports identity-safe rollback skip");
  std::filesystem::remove_all(directory);
}

void infrastructure_transaction_rolls_back_identity_probe_failure() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("graphx-infra-identity-failure-" + std::to_string(::getpid()));
  const auto first = directory / "first";
  const auto second = directory / "second";
  std::filesystem::create_directories(directory);
  const std::vector<graphx::InfraCommand> commands = {
      {{"touch", first.string()}, {}, false, {"rm", first.string()}, file_identity_command(first)},
      {{"touch", second.string()}, {}, false, {"rm", second.string()}, {"sh", "-c", "exit 23"}},
  };
  std::ostringstream output, errors;
  const auto status = graphx::execute_infrastructure_plan(commands, false, output, errors);
  expect(status == 23, "identity failure reports probe status");
  expect(!std::filesystem::exists(first) && !std::filesystem::exists(second),
         "identity failure removes current and completed resources");
  const auto log = output.str();
  expect(log.find("- rm " + second.string()) < log.find("- rm " + first.string()),
         "identity failure rolls back current resource first");
  std::filesystem::remove_all(directory);
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

void external_control_cycle_is_accepted() {
  TemporaryConfig file(R"yaml(
version: 1
graph:
  id: external-control-cycle
  nodes:
    - id: device
      kind: source
      runtime: external
      execution: host
      lifecycle: external
      control: none
      ports:
        - { name: samples, direction: output, schema: RawSamples }
        - { name: control, direction: input, schema: RawControl }
    - id: processor
      kind: transform
      ports:
        - { name: samples, direction: input, schema: RawSamples }
        - { name: control, direction: output, schema: RawControl }
  edges:
    - { id: samples, from: device.samples, to: processor.samples, transport: udp, data_plane: external }
    - { id: control, from: processor.control, to: device.control, transport: tcp, data_plane: external }
transport:
  tcp:
    control: { host: 127.0.0.1, bind: 0.0.0.0, port: 18401, framing: none }
  udp:
    samples: { mode: unicast, destination: 127.0.0.1, bind: 0.0.0.0, port: 18400, max_datagram_bytes: 1400, framing: none }
deployment:
  services:
    processor: { image: processor:latest, command: processor }
)yaml");
  const auto config = graphx::load_config(file.path());
  expect(config.node("device").runtime == "external", "external runtime loads");
  expect(config.edge("control").data_plane == "external", "external control edge loads");
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

std::string udp_config(std::string_view mode, std::string_view destination,
                       std::string_view extra = {}) {
  return "version: 1\n"
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

void external_data_plane_and_mixed_runtime_load() {
  TemporaryConfig valid(R"yaml(
version: 1
graph:
  id: external-runtime
  nodes:
    - id: origin
      kind: source
      runtime: docker
      execution: container
      lifecycle: managed
      control: origin
      ports: [{ name: out, direction: output, schema: RawBytes }]
    - id: guest
      kind: virtual-machine
      runtime: qemu
      execution: host
      lifecycle: external
      control: none
      accelerator: auto
      architecture: x86_64
      ports: [{ name: in, direction: input, schema: RawBytes }]
  edges:
    - { id: raw, from: origin.out, to: guest.in, transport: tcp, data_plane: external }
transport:
  tcp:
    raw: { host: 127.0.0.1, bind: 0.0.0.0, port: 18001, framing: none }
deployment:
  network: qemu-demo
  services:
    origin: { image: qemu-origin:latest, command: origin }
)yaml");
  const auto config = graphx::load_config(valid.path());
  expect(config.node("origin").runtime == "docker" &&
             config.node("origin").execution == "container" &&
             config.node("origin").control == "origin",
         "managed runtime metadata");
  expect(config.node("guest").runtime == "qemu" && config.node("guest").execution == "host" &&
             config.node("guest").lifecycle == "external" &&
             config.node("guest").control == "none" && config.node("guest").accelerator == "auto" &&
             config.node("guest").architecture == "x86_64",
         "external runtime metadata");
  expect(
      config.edge("raw").data_plane == "external" && config.edge("raw").transport.framing == "none",
      "raw data-plane metadata");

  graphx::TransportFactory factory;
  try {
    [[maybe_unused]] auto ignored =
        factory.create(config.edge("raw"), graphx::ConnectionMode::connect);
    throw std::runtime_error("external raw edge entered the transport factory");
  } catch (const std::invalid_argument& error) {
    expect(std::string(error.what()).find("external data-plane") != std::string::npos,
           "external factory rejection is actionable");
  }

  const auto changed = [](std::string source, std::string_view from, std::string_view to) {
    const auto position = source.find(from);
    if (position == std::string::npos) throw std::runtime_error("raw fixture edit failed");
    source.replace(position, from.size(), to);
    return source;
  };
  std::ifstream input(valid.path());
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const std::pair<std::string, std::string> invalid[] = {
      {changed(source, "framing: none", "framing: u32be"), ".framing"},
      {changed(source, ", data_plane: external", ""), ".framing"},
      {changed(source, "data_plane: external", "data_plane: invalid"), ".data_plane"},
      {changed(source, "runtime: qemu", "runtime: invalid"), ".runtime"},
      {changed(source, "execution: host", "execution: invalid"), ".execution"},
      {changed(source, "lifecycle: external", "lifecycle: invalid"), ".lifecycle"},
      {changed(source, "control: none", "control: invalid"), ".control"},
      {changed(source, "accelerator: auto", "accelerator: invalid"), ".accelerator"},
      {changed(source, "architecture: x86_64", "architecture: arm64"), ".architecture"},
      {changed(source, "lifecycle: external", "lifecycle: managed"),
       "missing placement for node 'guest'"},
      {changed(source, "services:\n    origin:",
               "services:\n    guest: { image: qemu-guest:latest, command: guest }\n    origin:"),
       "cannot manage a node whose lifecycle is 'external'"},
  };
  for (const auto& [contents, expected] : invalid) {
    TemporaryConfig file(contents);
    try {
      [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
      throw std::runtime_error("invalid external data-plane configuration was accepted");
    } catch (const graphx::ConfigError& error) {
      expect(diagnostic_contains(error, expected), "external data-plane diagnostic");
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
  std::cout << std::unitbuf;
  ::unsetenv("GRAPHX_OVERRIDES");
  const std::pair<const char*, std::function<void()>> tests[] = {
      {"authoritative config", authoritative_config_loads},
      {"version 2 profiles and attachments", version_two_profiles_and_attachments_load},
      {"strict version 2 planner boundary", version_two_is_strict_and_not_realized_by_v1_planner},
      {"deterministic version 1 migration", version_one_migration_is_deterministic},
      {"TCP policy", tcp_policy_loads},
      {"invalid TCP policy", invalid_tcp_policy_is_rejected},
      {"shared-memory config", shared_memory_config_loads},
      {"invalid observability", invalid_observability_is_rejected},
      {"capture config", capture_configuration},
      {"invalid operations config", invalid_operations_configuration_is_rejected},
      {"invalid history config", invalid_history_configuration_is_rejected},
      {"strict history scalar types", history_scalar_types_are_strict},
      {"strict history strings and keys", history_empty_and_unknown_values_are_strict},
      {"invalid control config", invalid_control_configuration_is_rejected},
      {"invalid shared-memory config", invalid_shared_memory_config_is_rejected},
      {"mixed network model", mixed_network_model_and_plan_load},
      {"standalone network examples", standalone_network_examples_load},
      {"static route policy model", static_route_policy_model_and_plan_load},
      {"infrastructure transaction rollback", infrastructure_transaction_rolls_back_in_reverse},
      {"infrastructure transaction replacement", infrastructure_transaction_preserves_replacement},
      {"infrastructure transaction identity failure",
       infrastructure_transaction_rolls_back_identity_probe_failure},
      {"invalid network reference", invalid_network_reference_is_rejected},
      {"override precedence", explicit_override_wins},
      {"invalid override", invalid_override_is_rejected},
      {"aggregated errors", semantic_errors_are_aggregated},
      {"cycle", cycle_is_rejected},
      {"external control cycle", external_control_cycle_is_accepted},
      {"invalid deployment", invalid_deployment_is_rejected},
      {"malformed and oversized", malformed_and_oversized_files_are_rejected},
      {"in-process queue config", in_process_queue_config_loads_and_validates},
      {"Unix socket deadline config", unix_socket_deadline_config_loads_and_validates},
      {"TCP TLS config", tcp_tls_config_loads_and_validates},
      {"UDP config", udp_configuration_loads_and_validates},
      {"external data plane", external_data_plane_and_mixed_runtime_load},
      {"in-process factory", in_process_factory_shares_named_channel},
      {"factory validation", factory_rejects_unvalidated_settings},
      {"TCP factory", tcp_factory_round_trip},
      {"Unix socket factory", unix_factory_round_trip},
      {"shared-memory factory", shared_memory_factory_round_trip},
      {"shared-memory factory timeout", shared_memory_factory_uses_connect_timeout},
      {"UDP factory", udp_factory_round_trip}};
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
