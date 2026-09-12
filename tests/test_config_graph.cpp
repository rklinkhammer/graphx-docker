#include "config_test_support.hpp"

using namespace std::chrono_literals;
using namespace config_test;

namespace {

void current_configuration_is_strict() {
  const std::pair<std::string, std::string> invalid[] = {
      {"version: 2", "version: \"2\""},
      {"profile: ethernet", "driver: bridge"},
      {"profile: ethernet", "profile: mystery"},
      {"profile: ethernet", "profile: macvlan"},
      {"external: false", "external: \"false\""},
      {"subnets: [10.80.0.0/24]", "subnets: [10.80.0.0/24, 10.81.0.0/24]"},
  };
  for (const auto& [from, to] : invalid) {
    auto source = current_network_config();
    source.replace(source.find(from), from.size(), to);
    TemporaryConfig file(source);
    try {
      [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
      throw std::runtime_error("invalid current configuration was accepted");
    } catch (const graphx::ConfigError&) {
    }
  }
}

void authoritative_config_loads() {
  const auto config = graphx::load_config(std::filesystem::path(GRAPHX_SOURCE_DIR) / "graphx.yaml");
  expect(config.version == 2 && config.id == "sample-pipeline", "root model");
  expect(config.nodes.size() == 3 && config.edges.size() == 2, "topology counts");
  expect(std::get<graphx::TcpTransportConfig>(config.edge("samples").transport).host == "transform",
         "TCP settings");
  expect(config.node("transform").ports.size() == 2, "node lookup");
  expect(config.deployment.services.size() == 3, "deployment placements");
  expect(config.deployment.services.front().node_id == "generator", "deployment separation");
  expect(config.network_infrastructure.networks.size() == 1, "network layer");
  expect(
      config.network_infrastructure.network("graphx").profile == graphx::NetworkProfile::ethernet,
      "Ethernet network model");
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

void explicit_override_wins() {
  TemporaryConfig file(valid_config);
  ::setenv("GRAPHX_OVERRIDES", "transport.tcp.sample-edge.host=environment", 1);
  const auto config = graphx::load_config(
      file.path(),
      {{"transport.tcp.sample-edge.host", "explicit"}, {"transport.tcp.sample-edge.port", "8123"}});
  ::unsetenv("GRAPHX_OVERRIDES");
  const auto& transport =
      std::get<graphx::TcpTransportConfig>(config.edge("sample-edge").transport);
  expect(transport.host == "explicit", "override precedence");
  expect(transport.port == 8123, "numeric override");
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
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path(), {{"version", "2"}});
    throw std::runtime_error("configuration version override was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "configuration version is immutable"),
           "immutable version override diagnostic");
  }
}

void semantic_errors_are_aggregated() {
  TemporaryConfig file(R"yaml(
version: 2
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
version: 2
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
version: 2
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
version: 2
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

void external_data_plane_and_mixed_runtime_load() {
  TemporaryConfig valid(R"yaml(
version: 2
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
      config.edge("raw").data_plane == "external" &&
          std::holds_alternative<graphx::ExternalTransportConfig>(config.edge("raw").transport) &&
          graphx::transport_framing(config.edge("raw").transport) == "none",
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

}  // namespace

int main() {
  return run_tests({
      {"authoritative config", authoritative_config_loads},
      {"strict current configuration", current_configuration_is_strict},
      {"override precedence", explicit_override_wins},
      {"invalid override", invalid_override_is_rejected},
      {"aggregated errors", semantic_errors_are_aggregated},
      {"cycle", cycle_is_rejected},
      {"external control cycle", external_control_cycle_is_accepted},
      {"invalid deployment", invalid_deployment_is_rejected},
      {"malformed and oversized", malformed_and_oversized_files_are_rejected},
      {"external data plane", external_data_plane_and_mixed_runtime_load},
  });
}
