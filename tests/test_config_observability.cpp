#include "config_test_support.hpp"

using namespace std::chrono_literals;
using namespace config_test;

namespace {

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

}  // namespace

int main() {
  return run_tests({
      {"invalid observability", invalid_observability_is_rejected},
      {"capture config", capture_configuration},
      {"invalid operations config", invalid_operations_configuration_is_rejected},
      {"invalid history config", invalid_history_configuration_is_rejected},
      {"strict history scalar types", history_scalar_types_are_strict},
      {"strict history strings and keys", history_empty_and_unknown_values_are_strict},
      {"invalid control config", invalid_control_configuration_is_rejected},
  });
}
