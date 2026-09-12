#include "config_test_support.hpp"
#include "graphx/normalized_config.hpp"

using namespace std::chrono_literals;
using namespace config_test;

namespace {

void instance_and_sdr_configuration() {
  const auto path =
      std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples/sdr-node/two-source/graphx.yaml";
  const auto config = graphx::load_config_literal(path);
  const auto& east = config.node_for_instance("lab-a", "sdr-east");
  const auto& west = config.node_for_instance("lab-a", "sdr-west");
  expect(east.sdr && west.sdr, "per-source typed SDR settings");
  expect(east.sdr->frequency_hz == 100000000 && west.sdr->frequency_hz == 200000000,
         "independent source processing settings");
  expect(east.sdr->samples_edge == "samples-east", "sample endpoint reference");
  expect(config.node("processor-east").sdr == std::nullopt, "ordinary nodes remain unchanged");
  for (const auto& selection :
       {std::pair{"lab-b", "sdr-east"}, {"", "sdr-east"}, {"lab-a", "missing"}}) {
    bool rejected = false;
    try {
      static_cast<void>(config.node_for_instance(selection.first, selection.second));
    } catch (const std::exception&) {
      rejected = true;
    }
    expect(rejected, "unknown or inconsistent selection must be refused");
  }
  ::setenv("GRAPHX_OVERRIDES", "deployment.instance_id=environment", 1);
  const auto explicit_config = graphx::load_config(path, {{"deployment.instance_id", "explicit"}});
  ::unsetenv("GRAPHX_OVERRIDES");
  expect(explicit_config.deployment.instance_id == "explicit", "instance override precedence");
  expect(graphx::load_config_literal(path).deployment.instance_id == "lab-a",
         "literal identity ignores overrides");
  for (const auto& id : {std::string("A"), std::string(64, 'A')}) {
    const auto selected = graphx::load_config(path, {{"deployment.instance_id", id}});
    expect(selected.deployment.instance_id == id, "identifier boundaries");
  }
  const auto normalized = graphx::normalize_config_json(explicit_config);
  expect(normalized.find("explicit") != std::string::npos, "resolved instance is normalized");
  expect(normalized.find("server.key") != std::string::npos,
         "credential reference is normalized without reading file");
  std::ifstream input(path);
  const std::string source((std::istreambuf_iterator<char>(input)), {});
  auto defaults = source;
  for (const auto& line : {std::string("        frequency_hz: 100000000\n"),
                           std::string("        sample_interval_ms: 200\n")}) {
    defaults.erase(defaults.find(line), line.size());
  }
  TemporaryConfig defaults_file(defaults);
  const auto defaults_config = graphx::load_config_literal(defaults_file.path());
  expect(defaults_config.node("sdr-east").sdr->frequency_hz == 100000000 &&
             defaults_config.node("sdr-east").sdr->sample_interval_ms == 200,
         "SDR defaults resolve in authoritative model");
  for (const auto& value : {std::string("true"), std::string("123")}) {
    auto scalar_source = source;
    const std::string field = "server_name: sdr-east";
    for (auto at = scalar_source.find(field); at != std::string::npos;
         at = scalar_source.find(field))
      scalar_source.replace(at, field.size(), "server_name: " + value);
    TemporaryConfig file(scalar_source);
    bool refused = false;
    try {
      static_cast<void>(graphx::load_config_literal(file.path()));
    } catch (const graphx::ConfigError& error) {
      refused = diagnostic_contains(error, "must be a string");
    }
    expect(refused, "credential reference scalar type must not be coerced");
  }
  const std::pair<std::string, std::string> invalid[] = {
      {"instance_id: lab-a", "instance_id: ''"},
      {"instance_id: lab-a", "instance_id: " + std::string(65, 'A')},
      {"instance_id: lab-a", "instance_id: '../lab'"},
      {"instance_id: lab-a", "instance_id: 'lab a'"},
      {"instance_id: lab-a", "instance_id: 'láb'"},
      {"instance_id: lab-a", "instance_id: true"},
      {"instance_id: lab-a", "instance_id: null"},
      {"instance_id: lab-a", "instance_id: [lab-a]"},
      {"instance_id: lab-a", "instance_id: lab-a\n  instance_id: lab-b"},
      {"instance_id: lab-a", "project: lab-a"},
      {"samples_edge: samples-east", "samples_edge: missing"},
      {"samples_edge: samples-east", "samples_edge: samples-west"},
      {"control_edge: control-east", "control_edge: samples-east"},
      {"to: processor-east.samples", "to: processor-west.samples"},
      {"frequency_hz: 100000000", "frequency_hz: 999999"},
      {"frequency_hz: 100000000", "frequency_hz: 6000000001"},
      {"frequency_hz: 100000000", "frequency_hz: '100000000'"},
      {"sample_interval_ms: 200", "sample_interval_ms: 19"},
      {"sample_interval_ms: 200", "sample_interval_ms: 60001"},
      {"sample_interval_ms: 200", "sample_interval_ms: true"},
      {"private_key_file: /run/sdr-east/server.key", "private_key: inline-secret"},
      {"private_key_file: /run/sdr-east/server.key", "private_key_file: relative.key"},
      {"server_name: sdr-east", "server_name: wrong-server"},
      {"server_name: sdr-east", "server_name: true"},
      {"server_name: sdr-east", "server_name: 123"},
      {"require_client_certificate: true", "require_client_certificate: false"},
      {"id: sdr-west", "id: sdr-east"},
  };
  for (const auto& [from, to] : invalid) {
    auto contents = source;
    const auto found = contents.find(from);
    expect(found != std::string::npos, "negative fixture replacement exists");
    contents.replace(found, from.size(), to);
    TemporaryConfig file(contents);
    bool rejected = false;
    try {
      static_cast<void>(graphx::load_config_literal(file.path()));
    } catch (const graphx::ConfigError&) {
      rejected = true;
    }
    expect(rejected, ("accepted invalid instance/SDR input: " + to).c_str());
  }
  TemporaryConfig legacy(valid_config);
  const auto legacy_config = graphx::load_config_literal(legacy.path());
  expect(legacy_config.deployment.instance_id.empty(), "no invented instance default");
  bool rejected = false;
  try {
    static_cast<void>(legacy_config.node_for_instance("lab-a", "source"));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  expect(rejected, "instance-aware lookup refuses missing instance");
}

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
      {"instance and SDR configuration", instance_and_sdr_configuration},
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
