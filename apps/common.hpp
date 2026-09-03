#pragma once

#include "graphx/capture.hpp"
#include "graphx/config.hpp"
#include "graphx/framing.hpp"
#include "graphx/observability.hpp"
#include "graphx/transport_factory.hpp"

#include <chrono>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <csignal>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace demo {

inline volatile std::sig_atomic_t shutdown_requested = 0;
inline void request_shutdown(int) { shutdown_requested = 1; }
inline void install_signal_handlers() {
  std::signal(SIGINT, request_shutdown);
  std::signal(SIGTERM, request_shutdown);
}
inline bool stopping() noexcept { return shutdown_requested != 0; }

inline void interruptible_pause(std::chrono::milliseconds duration) {
  constexpr auto quantum = std::chrono::milliseconds(50);
  while (!stopping() && duration.count() > 0) {
    const auto wait = std::min(duration, quantum);
    std::this_thread::sleep_for(wait);
    duration -= wait;
  }
}

inline std::string env(const char* name, std::string fallback) {
  if (const char* value = std::getenv(name)) return value;
  return fallback;
}

inline std::uint16_t port(const char* name, std::uint16_t fallback) {
  return static_cast<std::uint16_t>(std::stoi(env(name, std::to_string(fallback))));
}

inline bool boolean_env(const char* name, bool fallback) {
  const char* configured = std::getenv(name);
  if (!configured) return fallback;
  std::string value(configured);
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
  if (value == "0" || value == "false" || value == "no" || value == "off") return false;
  throw std::runtime_error(std::string(name) +
                           " must be one of true, false, 1, 0, yes, no, on, or off");
}

inline std::string capture_provider_env(std::string fallback) {
  auto provider = env("GRAPHX_CAPTURE_PROVIDER", std::move(fallback));
  if (!provider.empty() && provider != "pcapng" && provider != "ovs-span")
    throw std::runtime_error("GRAPHX_CAPTURE_PROVIDER must be 'pcapng' or 'ovs-span'");
  return provider;
}

inline std::uint64_t unsigned_env(const char* name, std::uint64_t fallback, std::uint64_t minimum,
                                  std::uint64_t maximum) {
  const auto value = env(name, std::to_string(fallback));
  std::uint64_t parsed{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size() || parsed < minimum ||
      parsed > maximum)
    throw std::runtime_error(std::string(name) + " must be between " + std::to_string(minimum) +
                             " and " + std::to_string(maximum));
  return parsed;
}

inline std::string secret_env(const char* name) {
  const auto file_name = std::string(name) + "_FILE";
  const char* inline_value = std::getenv(name);
  const char* file_value = std::getenv(file_name.c_str());
  const bool has_inline_value = inline_value && inline_value[0] != '\0';
  const bool has_file_value = file_value && file_value[0] != '\0';
  if (has_inline_value && has_file_value)
    throw std::runtime_error(std::string(name) + " and " + file_name + " are mutually exclusive");
  std::string value;
  if (has_inline_value) value = inline_value;
  if (has_file_value) {
    std::ifstream input(file_value, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read secret file for " + std::string(name));
    value.assign(std::istreambuf_iterator<char>(input), {});
    if (!value.empty() && value.back() == '\n') value.pop_back();
    if (!value.empty() && value.back() == '\r') value.pop_back();
  }
  if (value.size() > 4096) throw std::runtime_error(std::string(name) + " exceeds 4096 bytes");
  if (!value.empty() && value.size() < 32)
    throw std::runtime_error(std::string(name) + " must contain at least 32 bytes");
  return value;
}

class ConsoleTraceSink final : public graphx::TraceSink {
 public:
  void on_send(std::string_view edge, const graphx::Envelope& envelope,
               std::size_t bytes) override {
    std::cout << "metric edge=" << edge << " event=send seq=" << envelope.sequence
              << " bytes=" << bytes << std::endl;
  }
  void on_receive(std::string_view edge, const graphx::Envelope& envelope, std::size_t bytes,
                  std::chrono::nanoseconds latency) override {
    std::cout << "metric edge=" << edge << " event=receive seq=" << envelope.sequence
              << " bytes=" << bytes << " latency_us=" << latency.count() / 1000.0 << std::endl;
  }
  void on_error(std::string_view edge, std::string_view message) override {
    std::cerr << "metric edge=" << edge << " event=error message=\"" << message << "\"\n";
  }
  void on_connection(std::string_view edge, graphx::ConnectionState state) override {
    std::cout << "metric edge=" << edge << " event=connection state=" << graphx::to_string(state)
              << std::endl;
  }
  void on_reconnect(std::string_view edge) override {
    std::cout << "metric edge=" << edge << " event=reconnect" << std::endl;
  }
  void on_backpressure(std::string_view edge, std::chrono::nanoseconds duration,
                       bool rejected) override {
    std::cout << "metric edge=" << edge
              << " event=backpressure mode=" << (rejected ? "rejected" : "blocked")
              << " duration_us=" << duration.count() / 1000.0 << std::endl;
  }
  void on_processing(std::string_view node, const graphx::Envelope& envelope,
                     std::chrono::nanoseconds duration, bool success) override {
    std::cout << "metric node=" << node << " event=processing seq=" << envelope.sequence
              << " success=" << success << " duration_us=" << duration.count() / 1000.0
              << std::endl;
  }
};

class RuntimeTraceSink final : public graphx::TraceSink {
 public:
  RuntimeTraceSink(std::string node_id, const graphx::GraphConfig& config)
      : node_id_(std::move(node_id)),
        heartbeat_interval_(config.observability.telemetry.heartbeat_interval_ms) {
    const auto contains = [](const auto& signal, std::string_view exporter) {
      return signal.enabled &&
             std::ranges::find(signal.exporters, exporter) != signal.exporters.end();
    };
    if (contains(config.observability.metrics, "console") ||
        contains(config.observability.tracing, "console"))
      composite_.add(console_);
    if (config.observability.metrics.enabled) composite_.add(metrics_);
    if (contains(config.observability.metrics, "udp-json") ||
        contains(config.observability.tracing, "udp-json")) {
      telemetry_ = std::make_unique<graphx::UdpJsonTraceSink>(
          node_id_, env("GRAPHX_TELEMETRY_HOST", config.observability.telemetry.host),
          port("GRAPHX_TELEMETRY_PORT", config.observability.telemetry.port),
          secret_env("GRAPHX_TELEMETRY_SHARED_SECRET"));
      composite_.add(*telemetry_);
    }
    const auto otlp_host = env("GRAPHX_OTLP_HOST", "");
    if (!otlp_host.empty() || contains(config.observability.tracing, "otlp-http")) {
      const auto resolved_host = otlp_host.empty() ? std::string("127.0.0.1") : otlp_host;
      if (resolved_host != "127.0.0.1" && resolved_host != "localhost" && resolved_host != "::1")
        throw std::runtime_error(
            "native OTLP/HTTP export is limited to a loopback collector; use the telemetry "
            "service for authenticated TLS export");
      otlp_ = std::make_unique<graphx::OtlpHttpTraceSink>(node_id_, resolved_host,
                                                          port("GRAPHX_OTLP_PORT", 4318),
                                                          env("GRAPHX_OTLP_PATH", "/v1/traces"));
      composite_.add(*otlp_);
    }
    const auto capture_enabled =
        boolean_env("GRAPHX_CAPTURE_ENABLED", config.observability.capture.enabled);
    const auto capture_provider = capture_provider_env(config.observability.capture.provider);
    if (capture_enabled && capture_provider == "pcapng") {
      const auto directory = env("GRAPHX_CAPTURE_DIR", config.observability.capture.directory);
      const auto snaplen =
          unsigned_env("GRAPHX_CAPTURE_SNAPLEN", config.observability.capture.snaplen, 256,
                       16 * 1024 * 1024 + 4);
      const auto max_file_bytes =
          unsigned_env("GRAPHX_CAPTURE_MAX_FILE_BYTES", config.observability.capture.max_file_bytes,
                       65536, 4ULL * 1024 * 1024 * 1024);
      const auto max_packets = unsigned_env(
          "GRAPHX_CAPTURE_MAX_PACKETS", config.observability.capture.max_packets, 1, 100'000'000);
      capture_ = std::make_unique<graphx::PcapngCaptureSink>(
          std::filesystem::path(directory) / (node_id_ + ".pcapng"),
          static_cast<std::uint32_t>(snaplen), max_file_bytes, max_packets);
      std::cout << "capture node=" << node_id_ << " provider=pcapng path=" << capture_->path()
                << " snaplen=" << snaplen << " max_file_bytes=" << max_file_bytes
                << " max_packets=" << max_packets << std::endl;
    }
    heartbeat(true);
  }

  void heartbeat(bool force = false) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - last_heartbeat_ < heartbeat_interval_) return;
    const auto cpu_now = std::clock();
    const auto wall_seconds = std::chrono::duration<double>(now - last_cpu_sample_).count();
    double cpu_percent{};
    if (cpu_now != static_cast<std::clock_t>(-1) &&
        last_cpu_clock_ != static_cast<std::clock_t>(-1) && wall_seconds > 0.0) {
      const auto cpu_seconds =
          static_cast<double>(cpu_now - last_cpu_clock_) / static_cast<double>(CLOCKS_PER_SEC);
      cpu_percent = std::clamp(cpu_seconds / wall_seconds * 100.0, 0.0, 999.9);
    }
    last_heartbeat_ = now;
    last_cpu_sample_ = now;
    last_cpu_clock_ = cpu_now;
    composite_.on_heartbeat(node_id_, cpu_percent);
  }

  [[nodiscard]] bool paused() const noexcept { return telemetry_ && telemetry_->paused(); }

  void on_send(std::string_view edge, const graphx::Envelope& envelope,
               std::size_t bytes) override {
    composite_.on_send(edge, envelope, bytes);
    capture_frame(edge, envelope, graphx::CaptureSink::Direction::sent);
  }
  void on_receive(std::string_view edge, const graphx::Envelope& envelope, std::size_t bytes,
                  std::chrono::nanoseconds latency) override {
    composite_.on_receive(edge, envelope, bytes, latency);
    capture_frame(edge, envelope, graphx::CaptureSink::Direction::received);
  }
  void on_error(std::string_view edge, std::string_view message) override {
    composite_.on_error(edge, message);
  }
  void on_connection(std::string_view edge, graphx::ConnectionState state) override {
    composite_.on_connection(edge, state);
  }
  void on_reconnect(std::string_view edge) override { composite_.on_reconnect(edge); }
  void on_backpressure(std::string_view edge, std::chrono::nanoseconds duration,
                       bool rejected) override {
    composite_.on_backpressure(edge, duration, rejected);
  }
  void on_processing(std::string_view node, const graphx::Envelope& envelope,
                     std::chrono::nanoseconds duration, bool success) override {
    composite_.on_processing(node, envelope, duration, success);
  }
  void on_heartbeat(std::string_view node, double cpu_percent) override {
    composite_.on_heartbeat(node, cpu_percent);
  }

 private:
  void capture_frame(std::string_view edge, const graphx::Envelope& envelope,
                     graphx::CaptureSink::Direction direction) noexcept {
    if (!capture_) return;
    try {
      const auto framed = graphx::frame(graphx::serialize(envelope));
      capture_->record_frame(edge, framed, std::chrono::system_clock::now(),
                             {.direction = direction,
                              .sequence = envelope.sequence,
                              .wire_version = envelope.wire_version,
                              .message_id = envelope.message_id,
                              .parent_message_id = envelope.parent_message_id,
                              .trace_id = envelope.trace_id,
                              .type = envelope.type});
      if (telemetry_) {
        const auto direction_name =
            direction == graphx::CaptureSink::Direction::sent ? "sent" : "received";
        telemetry_->on_capture(edge, envelope, direction_name, capture_->path().filename().string(),
                               capture_->packet_count(), capture_->last_packet_offset());
      }
    } catch (const std::exception& error) {
      std::cerr << "capture node=" << node_id_ << " event=error message=\"" << error.what()
                << "\"\n";
      capture_.reset();
    }
  }

  std::string node_id_;
  std::chrono::milliseconds heartbeat_interval_;
  std::chrono::steady_clock::time_point last_heartbeat_{};
  std::chrono::steady_clock::time_point last_cpu_sample_{std::chrono::steady_clock::now()};
  std::clock_t last_cpu_clock_{std::clock()};
  ConsoleTraceSink console_;
  graphx::MetricsTraceSink metrics_;
  std::unique_ptr<graphx::UdpJsonTraceSink> telemetry_;
  std::unique_ptr<graphx::OtlpHttpTraceSink> otlp_;
  std::unique_ptr<graphx::PcapngCaptureSink> capture_;
  graphx::CompositeTraceSink composite_;
};

inline std::filesystem::path config_path() { return env("GRAPHX_CONFIG", "graphx.yaml"); }

}  // namespace demo
