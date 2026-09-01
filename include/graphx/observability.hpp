#pragma once

#include "graphx/envelope.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace graphx {

enum class ConnectionState { disconnected, connecting, listening, connected, closed, error };
std::string_view to_string(ConnectionState state) noexcept;

class TraceSink {
 public:
  virtual ~TraceSink() = default;
  virtual void on_send(std::string_view edge_id, const Envelope& envelope,
                       std::size_t wire_bytes) = 0;
  virtual void on_receive(std::string_view edge_id, const Envelope& envelope,
                          std::size_t wire_bytes, std::chrono::nanoseconds latency) = 0;
  virtual void on_error(std::string_view edge_id, std::string_view message) = 0;
  virtual void on_connection(std::string_view, ConnectionState) {}
  virtual void on_reconnect(std::string_view) {}
  virtual void on_backpressure(std::string_view, std::chrono::nanoseconds, bool) {}
  virtual void on_processing(std::string_view, const Envelope&, std::chrono::nanoseconds, bool) {}
  virtual void on_heartbeat(std::string_view, double) {}
};

class NullTraceSink final : public TraceSink {
 public:
  void on_send(std::string_view, const Envelope&, std::size_t) override {}
  void on_receive(std::string_view, const Envelope&, std::size_t,
                  std::chrono::nanoseconds) override {}
  void on_error(std::string_view, std::string_view) override {}
};

struct EdgeMetrics {
  std::uint64_t sent{};
  std::uint64_t received{};
  std::uint64_t sent_wire_bytes{};
  std::uint64_t received_wire_bytes{};
  std::uint64_t errors{};
  std::uint64_t reconnects{};
  std::uint64_t backpressure_events{};
  std::uint64_t rejected{};
  std::chrono::nanoseconds total_backpressure{};
  std::chrono::nanoseconds total_latency{};
  std::array<std::uint64_t, 8> latency_buckets{};
  ConnectionState connection{ConnectionState::disconnected};
};

class MetricsTraceSink final : public TraceSink {
 public:
  void on_send(std::string_view edge_id, const Envelope&, std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope&, std::size_t wire_bytes,
                  std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view) override;
  void on_connection(std::string_view edge_id, ConnectionState state) override;
  void on_reconnect(std::string_view edge_id) override;
  void on_backpressure(std::string_view edge_id, std::chrono::nanoseconds duration,
                       bool rejected) override;
  EdgeMetrics edge(std::string_view edge_id) const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, EdgeMetrics> edges_;
};

class CompositeTraceSink final : public TraceSink {
 public:
  void add(TraceSink& sink) { sinks_.push_back(&sink); }
  void on_send(std::string_view edge_id, const Envelope& envelope, std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope& envelope, std::size_t wire_bytes,
                  std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view message) override;
  void on_connection(std::string_view edge_id, ConnectionState state) override;
  void on_reconnect(std::string_view edge_id) override;
  void on_backpressure(std::string_view edge_id, std::chrono::nanoseconds duration,
                       bool rejected) override;
  void on_processing(std::string_view node_id, const Envelope& envelope,
                     std::chrono::nanoseconds duration, bool success) override;
  void on_heartbeat(std::string_view node_id, double cpu_percent) override;

 private:
  std::vector<TraceSink*> sinks_;
};

// Best-effort event exporter. UDP loss or collector downtime never stops graph
// processing; a production adapter can replace this behind TraceSink.
class UdpJsonTraceSink final : public TraceSink {
 public:
  UdpJsonTraceSink(std::string node_id, std::string host, std::uint16_t port);
  ~UdpJsonTraceSink() override;
  UdpJsonTraceSink(const UdpJsonTraceSink&) = delete;
  UdpJsonTraceSink& operator=(const UdpJsonTraceSink&) = delete;

  void on_send(std::string_view edge_id, const Envelope& envelope, std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope& envelope, std::size_t wire_bytes,
                  std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view message) override;
  void on_connection(std::string_view edge_id, ConnectionState state) override;
  void on_reconnect(std::string_view edge_id) override;
  void on_backpressure(std::string_view edge_id, std::chrono::nanoseconds duration,
                       bool rejected) override;
  void on_processing(std::string_view node_id, const Envelope& envelope,
                     std::chrono::nanoseconds duration, bool success) override;
  void on_heartbeat(std::string_view node_id, double cpu_percent) override;
  void on_capture(std::string_view edge_id, const Envelope& envelope, std::string_view direction,
                  std::string_view file, std::uint64_t packet_index, std::uint64_t file_offset);
  // Runtime commands are accepted only from the connected telemetry peer.
  // Pause is advisory: source nodes stop producing new envelopes while
  // in-flight work is allowed to drain.
  [[nodiscard]] bool paused() const noexcept;

 private:
  void emit(std::string_view event, std::string_view edge_id, const Envelope* envelope,
            std::size_t wire_bytes, std::chrono::nanoseconds latency, std::string_view message = {},
            double cpu_percent = -1.0);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Optional, bounded, asynchronous OTLP/HTTP JSON span exporter. It is disabled
// unless an application constructs it (the demo runtime uses GRAPHX_OTLP_HOST).
class OtlpHttpTraceSink final : public TraceSink {
 public:
  OtlpHttpTraceSink(std::string node_id, std::string host, std::uint16_t port = 4318,
                    std::string path = "/v1/traces", std::size_t queue_capacity = 1024);
  ~OtlpHttpTraceSink() override;
  OtlpHttpTraceSink(const OtlpHttpTraceSink&) = delete;
  OtlpHttpTraceSink& operator=(const OtlpHttpTraceSink&) = delete;

  void on_send(std::string_view edge_id, const Envelope& envelope, std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope& envelope, std::size_t wire_bytes,
                  std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view message) override;
  void on_processing(std::string_view node_id, const Envelope& envelope,
                     std::chrono::nanoseconds duration, bool success) override;

 private:
  void enqueue_span(std::string_view name, std::string_view subject, const Envelope* envelope,
                    std::chrono::nanoseconds duration, std::string_view status,
                    std::size_t wire_bytes = 0);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class CaptureSink {
 public:
  enum class Direction { sent, received };

  struct Metadata {
    Direction direction{Direction::sent};
    std::uint64_t sequence{};
    std::uint8_t wire_version{};
    std::string_view message_id;
    std::string_view parent_message_id;
    std::string_view trace_id;
    std::string_view type;
  };

  virtual ~CaptureSink() = default;
  virtual void record_frame(std::string_view edge_id, std::span<const std::byte> frame,
                            std::chrono::system_clock::time_point timestamp,
                            const Metadata& metadata) = 0;
};

// Extension point for a Wireshark extcap control pipe or another live source.
class ExtcapProvider {
 public:
  virtual ~ExtcapProvider() = default;
  virtual std::string_view interface_name() const = 0;
  virtual void start(CaptureSink& sink) = 0;
  virtual void stop() = 0;
};

}  // namespace graphx
