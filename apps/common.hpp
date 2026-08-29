#pragma once

#include "graphx/config.hpp"
#include "graphx/observability.hpp"
#include "graphx/transport_factory.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace demo {

inline std::string env(const char* name, std::string fallback) {
  if (const char* value = std::getenv(name)) return value;
  return fallback;
}

inline std::uint16_t port(const char* name, std::uint16_t fallback) {
  return static_cast<std::uint16_t>(std::stoi(env(name, std::to_string(fallback))));
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
    std::cout << "metric edge=" << edge << " event=connection state="
              << graphx::to_string(state) << std::endl;
  }
  void on_reconnect(std::string_view edge) override {
    std::cout << "metric edge=" << edge << " event=reconnect" << std::endl;
  }
  void on_backpressure(std::string_view edge, std::chrono::nanoseconds duration,
                       bool rejected) override {
    std::cout << "metric edge=" << edge << " event=backpressure mode="
              << (rejected ? "rejected" : "blocked")
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
  explicit RuntimeTraceSink(std::string node_id)
      : telemetry_(node_id, env("GRAPHX_TELEMETRY_HOST", "127.0.0.1"),
                   port("GRAPHX_TELEMETRY_PORT", 9000)) {
    composite_.add(console_);
    composite_.add(metrics_);
    composite_.add(telemetry_);
    const auto otlp_host = env("GRAPHX_OTLP_HOST", "");
    if (!otlp_host.empty()) {
      otlp_ = std::make_unique<graphx::OtlpHttpTraceSink>(
          std::move(node_id), otlp_host, port("GRAPHX_OTLP_PORT", 4318),
          env("GRAPHX_OTLP_PATH", "/v1/traces"));
      composite_.add(*otlp_);
    }
  }

  void on_send(std::string_view edge, const graphx::Envelope& envelope,
               std::size_t bytes) override {
    composite_.on_send(edge, envelope, bytes);
  }
  void on_receive(std::string_view edge, const graphx::Envelope& envelope, std::size_t bytes,
                  std::chrono::nanoseconds latency) override {
    composite_.on_receive(edge, envelope, bytes, latency);
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

 private:
  ConsoleTraceSink console_;
  graphx::MetricsTraceSink metrics_;
  graphx::UdpJsonTraceSink telemetry_;
  std::unique_ptr<graphx::OtlpHttpTraceSink> otlp_;
  graphx::CompositeTraceSink composite_;
};

inline std::filesystem::path config_path() { return env("GRAPHX_CONFIG", "graphx.yaml"); }

}  // namespace demo
