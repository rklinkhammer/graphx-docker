#include "config_internal.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace graphx::config_internal {

void ConfigParser::parse_signal(const YAML::Node& value, const std::string& path,
                                ObservabilitySignalConfig& signal, bool allow_otlp) {
  if (!value) return;
  if (!require_map(value, path)) return;
  strict_keys(value, path, {"enabled", "exporters"});
  signal.enabled = bool_value(value["enabled"], path + ".enabled", true);
  if (!value["exporters"]) return;
  if (!require_sequence(value["exporters"], path + ".exporters")) return;
  signal.exporters.clear();
  std::unordered_set<std::string> seen;
  for (std::size_t index = 0; index < value["exporters"].size(); ++index) {
    const auto item_path = path + ".exporters[" + std::to_string(index) + "]";
    auto exporter = text(value["exporters"][index], item_path, 32);
    if (exporter != "console" && exporter != "udp-json" && (!allow_otlp || exporter != "otlp-http"))
      error(item_path, allow_otlp ? "must be 'console', 'udp-json', or 'otlp-http'"
                                  : "must be 'console' or 'udp-json'");
    if (!exporter.empty() && !seen.insert(exporter).second)
      error(item_path, "duplicate exporter '" + exporter + "'");
    signal.exporters.push_back(std::move(exporter));
  }
  if (signal.enabled && signal.exporters.empty())
    error(path + ".exporters", "must not be empty when enabled");
}

void ConfigParser::parse_observability(const YAML::Node& value, GraphConfig& config) {
  if (!value) return;
  if (!require_map(value, "observability")) return;
  strict_keys(value, "observability",
              {"metrics", "tracing", "telemetry", "capture", "otlp", "slos", "history", "control"});
  parse_signal(value["metrics"], "observability.metrics", config.observability.metrics, false);
  parse_signal(value["tracing"], "observability.tracing", config.observability.tracing, true);
  if (const auto telemetry = value["telemetry"]) {
    if (require_map(telemetry, "observability.telemetry")) {
      strict_keys(telemetry, "observability.telemetry",
                  {"host", "port", "websocket", "heartbeat_interval_ms", "heartbeat_timeout_ms"});
      if (telemetry["host"])
        config.observability.telemetry.host =
            text(telemetry["host"], "observability.telemetry.host", 253);
      if (telemetry["port"]) {
        const auto port = unsigned_value(telemetry["port"], "observability.telemetry.port");
        if (port == 0 || port > 65535)
          error("observability.telemetry.port", "must be between 1 and 65535");
        else
          config.observability.telemetry.port = static_cast<std::uint16_t>(port);
      }
      if (telemetry["websocket"]) {
        config.observability.telemetry.websocket =
            text(telemetry["websocket"], "observability.telemetry.websocket", 128);
        if (!config.observability.telemetry.websocket.starts_with('/'))
          error("observability.telemetry.websocket", "must start with '/'");
      }
      if (telemetry["heartbeat_interval_ms"])
        config.observability.telemetry.heartbeat_interval_ms = unsigned_value(
            telemetry["heartbeat_interval_ms"], "observability.telemetry.heartbeat_interval_ms");
      if (telemetry["heartbeat_timeout_ms"])
        config.observability.telemetry.heartbeat_timeout_ms = unsigned_value(
            telemetry["heartbeat_timeout_ms"], "observability.telemetry.heartbeat_timeout_ms");
      if (config.observability.telemetry.heartbeat_interval_ms == 0 ||
          config.observability.telemetry.heartbeat_interval_ms > 600000)
        error("observability.telemetry.heartbeat_interval_ms", "must be between 1 and 600000");
      if (config.observability.telemetry.heartbeat_timeout_ms <
              config.observability.telemetry.heartbeat_interval_ms * 2ULL ||
          config.observability.telemetry.heartbeat_timeout_ms > 3600000)
        error("observability.telemetry.heartbeat_timeout_ms",
              "must be at least twice heartbeat_interval_ms and at most 3600000");
    }
  }
  if (const auto capture = value["capture"]) {
    if (require_map(capture, "observability.capture")) {
      strict_keys(capture, "observability.capture",
                  {"enabled", "provider", "directory", "snaplen", "max_file_bytes", "max_packets"});
      config.observability.capture.enabled =
          strict_bool_value(capture["enabled"], "observability.capture.enabled", false);
      if (capture["provider"])
        config.observability.capture.provider =
            text(capture["provider"], "observability.capture.provider", 128);
      if (capture["directory"])
        config.observability.capture.directory =
            text(capture["directory"], "observability.capture.directory", 1024);
      if (capture["snaplen"])
        config.observability.capture.snaplen =
            strict_unsigned_value(capture["snaplen"], "observability.capture.snaplen");
      if (capture["max_file_bytes"])
        config.observability.capture.max_file_bytes = strict_unsigned_64_value(
            capture["max_file_bytes"], "observability.capture.max_file_bytes");
      if (capture["max_packets"])
        config.observability.capture.max_packets =
            strict_unsigned_value(capture["max_packets"], "observability.capture.max_packets");
      if (config.observability.capture.enabled && config.observability.capture.provider.empty())
        error("observability.capture.provider", "is required when capture is enabled");
      if (!config.observability.capture.provider.empty() &&
          config.observability.capture.provider != "pcapng" &&
          config.observability.capture.provider != "ovs-span")
        error("observability.capture.provider", "must be 'pcapng' or 'ovs-span'");
      if (config.observability.capture.enabled &&
          config.observability.capture.provider == "pcapng" && !capture["directory"])
        error("observability.capture.directory", "is required for the pcapng provider");
      if (config.observability.capture.snaplen < 256 ||
          config.observability.capture.snaplen > 16 * 1024 * 1024 + 4)
        error("observability.capture.snaplen", "must be between 256 and 16777220");
      if (config.observability.capture.max_file_bytes < 65536 ||
          config.observability.capture.max_file_bytes > 4ULL * 1024 * 1024 * 1024)
        error("observability.capture.max_file_bytes", "must be between 65536 and 4294967296");
      if (config.observability.capture.max_packets == 0 ||
          config.observability.capture.max_packets > 100'000'000)
        error("observability.capture.max_packets", "must be between 1 and 100000000");
    }
  }
  if (const auto otlp = value["otlp"]) {
    if (require_map(otlp, "observability.otlp")) {
      strict_keys(otlp, "observability.otlp",
                  {"enabled", "endpoint", "traces_path", "metrics_path", "export_interval_ms",
                   "timeout_ms", "queue_capacity", "max_queue_bytes", "max_response_bytes",
                   "retry_max_attempts", "retry_initial_backoff_ms", "retry_max_backoff_ms"});
      auto& result = config.observability.otlp;
      result.enabled = bool_value(otlp["enabled"], "observability.otlp.enabled", false);
      if (otlp["endpoint"])
        result.endpoint = text(otlp["endpoint"], "observability.otlp.endpoint", 2048);
      if (otlp["traces_path"])
        result.traces_path = text(otlp["traces_path"], "observability.otlp.traces_path", 256);
      if (otlp["metrics_path"])
        result.metrics_path = text(otlp["metrics_path"], "observability.otlp.metrics_path", 256);
      if (!result.traces_path.starts_with('/'))
        error("observability.otlp.traces_path", "must start with '/'");
      if (!result.metrics_path.starts_with('/'))
        error("observability.otlp.metrics_path", "must start with '/'");
      std::smatch endpoint_match;
      if (!std::regex_match(result.endpoint, endpoint_match, kHttpOrigin)) {
        error("observability.otlp.endpoint",
              "must be an HTTP(S) origin without credentials or a path");
      } else if (endpoint_match[3].matched) {
        unsigned endpoint_port{};
        const auto port_text = endpoint_match[3].str();
        const auto conversion =
            std::from_chars(port_text.data(), port_text.data() + port_text.size(), endpoint_port);
        if (conversion.ec != std::errc{} || endpoint_port == 0 || endpoint_port > 65535)
          error("observability.otlp.endpoint", "port must be between 1 and 65535");
      }
      if (!std::regex_match(result.traces_path, std::regex{"^/[A-Za-z0-9._~/-]*$"}))
        error("observability.otlp.traces_path", "contains unsupported characters");
      if (!std::regex_match(result.metrics_path, std::regex{"^/[A-Za-z0-9._~/-]*$"}))
        error("observability.otlp.metrics_path", "contains unsupported characters");
      if (otlp["export_interval_ms"])
        result.export_interval_ms =
            unsigned_value(otlp["export_interval_ms"], "observability.otlp.export_interval_ms");
      if (otlp["timeout_ms"])
        result.timeout_ms = unsigned_value(otlp["timeout_ms"], "observability.otlp.timeout_ms");
      if (otlp["queue_capacity"])
        result.queue_capacity =
            unsigned_value(otlp["queue_capacity"], "observability.otlp.queue_capacity");
      if (otlp["max_queue_bytes"])
        result.max_queue_bytes =
            unsigned_value(otlp["max_queue_bytes"], "observability.otlp.max_queue_bytes");
      if (otlp["max_response_bytes"])
        result.max_response_bytes =
            unsigned_value(otlp["max_response_bytes"], "observability.otlp.max_response_bytes");
      if (otlp["retry_max_attempts"])
        result.retry_max_attempts =
            unsigned_value(otlp["retry_max_attempts"], "observability.otlp.retry_max_attempts");
      if (otlp["retry_initial_backoff_ms"])
        result.retry_initial_backoff_ms = unsigned_value(
            otlp["retry_initial_backoff_ms"], "observability.otlp.retry_initial_backoff_ms");
      if (otlp["retry_max_backoff_ms"])
        result.retry_max_backoff_ms =
            unsigned_value(otlp["retry_max_backoff_ms"], "observability.otlp.retry_max_backoff_ms");
      if (result.export_interval_ms < 250 || result.export_interval_ms > 600000)
        error("observability.otlp.export_interval_ms", "must be between 250 and 600000");
      if (result.timeout_ms < 100 || result.timeout_ms > 60000)
        error("observability.otlp.timeout_ms", "must be between 100 and 60000");
      if (result.queue_capacity == 0 || result.queue_capacity > 65536)
        error("observability.otlp.queue_capacity", "must be between 1 and 65536");
      if (result.max_queue_bytes < 65536 || result.max_queue_bytes > 64 * 1024 * 1024)
        error("observability.otlp.max_queue_bytes", "must be between 65536 and 67108864");
      if (result.max_response_bytes < 1024 || result.max_response_bytes > 4 * 1024 * 1024)
        error("observability.otlp.max_response_bytes", "must be between 1024 and 4194304");
      if (result.retry_max_attempts == 0 || result.retry_max_attempts > 10)
        error("observability.otlp.retry_max_attempts", "must be between 1 and 10");
      if (result.retry_initial_backoff_ms < 10 || result.retry_initial_backoff_ms > 60000)
        error("observability.otlp.retry_initial_backoff_ms", "must be between 10 and 60000");
      if (result.retry_max_backoff_ms < 10 || result.retry_max_backoff_ms > 600000)
        error("observability.otlp.retry_max_backoff_ms", "must be between 10 and 600000");
      if (result.retry_max_backoff_ms < result.retry_initial_backoff_ms)
        error("observability.otlp.retry_max_backoff_ms",
              "must not be less than retry_initial_backoff_ms");
    }
  }
  if (const auto slos = value["slos"]) {
    if (require_map(slos, "observability.slos")) {
      strict_keys(slos, "observability.slos",
                  {"window_seconds", "minimum_window_seconds", "availability_target",
                   "max_error_ratio", "max_drop_ratio", "max_p95_latency_us"});
      auto& result = config.observability.slos;
      if (slos["window_seconds"])
        result.window_seconds =
            unsigned_value(slos["window_seconds"], "observability.slos.window_seconds");
      if (slos["minimum_window_seconds"])
        result.minimum_window_seconds = unsigned_value(slos["minimum_window_seconds"],
                                                       "observability.slos.minimum_window_seconds");
      if (slos["availability_target"])
        result.availability_target = double_value(slos["availability_target"],
                                                  "observability.slos.availability_target", 0.99);
      if (slos["max_error_ratio"])
        result.max_error_ratio =
            double_value(slos["max_error_ratio"], "observability.slos.max_error_ratio", 0.01);
      if (slos["max_drop_ratio"])
        result.max_drop_ratio =
            double_value(slos["max_drop_ratio"], "observability.slos.max_drop_ratio", 0.01);
      if (slos["max_p95_latency_us"])
        result.max_p95_latency_us =
            unsigned_value(slos["max_p95_latency_us"], "observability.slos.max_p95_latency_us");
      if (result.window_seconds < 10 || result.window_seconds > 3600)
        error("observability.slos.window_seconds", "must be between 10 and 3600");
      if (result.minimum_window_seconds == 0 ||
          result.minimum_window_seconds > result.window_seconds)
        error("observability.slos.minimum_window_seconds", "must be between 1 and window_seconds");
      if (result.availability_target < 0 || result.availability_target > 1)
        error("observability.slos.availability_target", "must be between 0 and 1");
      if (result.max_error_ratio < 0 || result.max_error_ratio > 1)
        error("observability.slos.max_error_ratio", "must be between 0 and 1");
      if (result.max_drop_ratio < 0 || result.max_drop_ratio > 1)
        error("observability.slos.max_drop_ratio", "must be between 0 and 1");
      if (result.max_p95_latency_us == 0 || result.max_p95_latency_us > 3600000000ULL)
        error("observability.slos.max_p95_latency_us", "must be between 1 and 3600000000");
    }
  }
  if (const auto history = value["history"]) {
    if (require_map(history, "observability.history")) {
      strict_keys(history, "observability.history",
                  {"enabled", "backend", "database_file", "retention_seconds", "max_records",
                   "max_database_bytes", "queue_capacity", "max_queue_bytes", "batch_size",
                   "flush_interval_ms", "query_limit", "query_timeout_ms", "max_pending_queries",
                   "shutdown_timeout_ms"});
      auto& result = config.observability.history;
      result.enabled =
          strict_bool_value(history["enabled"], "observability.history.enabled", false);
      if (history["backend"])
        result.backend = text(history["backend"], "observability.history.backend", 32);
      if (history["database_file"])
        result.database_file =
            text(history["database_file"], "observability.history.database_file", 1024);
      if (history["retention_seconds"])
        result.retention_seconds = strict_unsigned_value(history["retention_seconds"],
                                                         "observability.history.retention_seconds");
      if (history["max_records"])
        result.max_records =
            strict_unsigned_64_value(history["max_records"], "observability.history.max_records");
      if (history["max_database_bytes"])
        result.max_database_bytes = strict_unsigned_64_value(
            history["max_database_bytes"], "observability.history.max_database_bytes");
      if (history["queue_capacity"])
        result.queue_capacity = strict_unsigned_value(history["queue_capacity"],
                                                      "observability.history.queue_capacity");
      if (history["max_queue_bytes"])
        result.max_queue_bytes = strict_unsigned_value(history["max_queue_bytes"],
                                                       "observability.history.max_queue_bytes");
      if (history["batch_size"])
        result.batch_size =
            strict_unsigned_value(history["batch_size"], "observability.history.batch_size");
      if (history["flush_interval_ms"])
        result.flush_interval_ms = strict_unsigned_value(history["flush_interval_ms"],
                                                         "observability.history.flush_interval_ms");
      if (history["query_limit"])
        result.query_limit =
            strict_unsigned_value(history["query_limit"], "observability.history.query_limit");
      if (history["query_timeout_ms"])
        result.query_timeout_ms = strict_unsigned_value(history["query_timeout_ms"],
                                                        "observability.history.query_timeout_ms");
      if (history["max_pending_queries"])
        result.max_pending_queries = strict_unsigned_value(
            history["max_pending_queries"], "observability.history.max_pending_queries");
      if (history["shutdown_timeout_ms"])
        result.shutdown_timeout_ms = strict_unsigned_value(
            history["shutdown_timeout_ms"], "observability.history.shutdown_timeout_ms");

      if (result.backend != "sqlite") error("observability.history.backend", "must be 'sqlite'");
      if (result.retention_seconds < 60 || result.retention_seconds > 31536000)
        error("observability.history.retention_seconds", "must be between 60 and 31536000");
      if (result.max_records < 10 || result.max_records > 10000000)
        error("observability.history.max_records", "must be between 10 and 10000000");
      if (result.max_database_bytes < 1024 * 1024 ||
          result.max_database_bytes > 4ULL * 1024 * 1024 * 1024)
        error("observability.history.max_database_bytes", "must be between 1048576 and 4294967296");
      if (result.queue_capacity == 0 || result.queue_capacity > 65536)
        error("observability.history.queue_capacity", "must be between 1 and 65536");
      if (result.max_queue_bytes < 65536 || result.max_queue_bytes > 64 * 1024 * 1024)
        error("observability.history.max_queue_bytes", "must be between 65536 and 67108864");
      if (result.batch_size == 0 || result.batch_size > 1000)
        error("observability.history.batch_size", "must be between 1 and 1000");
      if (result.batch_size > result.queue_capacity)
        error("observability.history.batch_size", "must not exceed queue_capacity");
      if (result.flush_interval_ms < 10 || result.flush_interval_ms > 60000)
        error("observability.history.flush_interval_ms", "must be between 10 and 60000");
      if (result.query_limit == 0 || result.query_limit > 1000)
        error("observability.history.query_limit", "must be between 1 and 1000");
      if (result.query_timeout_ms < 100 || result.query_timeout_ms > 10000)
        error("observability.history.query_timeout_ms", "must be between 100 and 10000");
      if (result.max_pending_queries == 0 || result.max_pending_queries > 128)
        error("observability.history.max_pending_queries", "must be between 1 and 128");
      if (result.shutdown_timeout_ms < 100 || result.shutdown_timeout_ms > 10000)
        error("observability.history.shutdown_timeout_ms", "must be between 100 and 10000");
    }
  }
  if (const auto control = value["control"]) {
    if (require_map(control, "observability.control")) {
      strict_keys(control, "observability.control",
                  {"command_timeout_ms", "command_retention_seconds", "max_commands",
                   "max_audit_records", "idempotency_ttl_seconds", "max_request_bytes"});
      auto& result = config.observability.control;
      const auto assign = [&](std::string_view name, std::uint32_t& destination) {
        const auto key = std::string(name);
        if (control[key])
          destination = strict_unsigned_value(control[key], "observability.control." + key);
      };
      assign("command_timeout_ms", result.command_timeout_ms);
      assign("command_retention_seconds", result.command_retention_seconds);
      assign("max_commands", result.max_commands);
      assign("max_audit_records", result.max_audit_records);
      assign("idempotency_ttl_seconds", result.idempotency_ttl_seconds);
      assign("max_request_bytes", result.max_request_bytes);
      if (result.command_timeout_ms < 100 || result.command_timeout_ms > 30000)
        error("observability.control.command_timeout_ms", "must be between 100 and 30000");
      if (result.command_retention_seconds < 60 || result.command_retention_seconds > 86400)
        error("observability.control.command_retention_seconds", "must be between 60 and 86400");
      if (result.max_commands == 0 || result.max_commands > 10000)
        error("observability.control.max_commands", "must be between 1 and 10000");
      if (result.max_audit_records < 10 || result.max_audit_records > 100000)
        error("observability.control.max_audit_records", "must be between 10 and 100000");
      if (result.idempotency_ttl_seconds < 60 || result.idempotency_ttl_seconds > 86400)
        error("observability.control.idempotency_ttl_seconds", "must be between 60 and 86400");
      if (result.max_request_bytes < 256 || result.max_request_bytes > 16384)
        error("observability.control.max_request_bytes", "must be between 256 and 16384");
    }
  }
}

}  // namespace graphx::config_internal
