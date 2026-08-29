#pragma once

#include "graphx/envelope.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace graphx {

class TraceSink {
 public:
  virtual ~TraceSink() = default;
  virtual void on_send(std::string_view edge_id, const Envelope& envelope,
                       std::size_t wire_bytes) = 0;
  virtual void on_receive(std::string_view edge_id, const Envelope& envelope,
                          std::size_t wire_bytes,
                          std::chrono::nanoseconds latency) = 0;
  virtual void on_error(std::string_view edge_id, std::string_view message) = 0;
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
  std::uint64_t wire_bytes{};
  std::uint64_t errors{};
  std::chrono::nanoseconds total_latency{};
};

class MetricsTraceSink final : public TraceSink {
 public:
  void on_send(std::string_view edge_id, const Envelope&, std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope&, std::size_t wire_bytes,
                  std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view) override;
  EdgeMetrics edge(std::string_view edge_id) const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, EdgeMetrics> edges_;
};

class CompositeTraceSink final : public TraceSink {
 public:
  void add(TraceSink& sink) { sinks_.push_back(&sink); }
  void on_send(std::string_view edge_id, const Envelope& envelope,
               std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope& envelope,
                  std::size_t wire_bytes, std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view message) override;

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

  void on_send(std::string_view edge_id, const Envelope& envelope,
               std::size_t wire_bytes) override;
  void on_receive(std::string_view edge_id, const Envelope& envelope,
                  std::size_t wire_bytes, std::chrono::nanoseconds latency) override;
  void on_error(std::string_view edge_id, std::string_view message) override;

 private:
  void emit(std::string_view event, std::string_view edge_id, const Envelope* envelope,
            std::size_t wire_bytes, std::chrono::nanoseconds latency,
            std::string_view message = {});
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class CaptureSink {
 public:
  virtual ~CaptureSink() = default;
  virtual void record_frame(std::string_view edge_id,
                            std::span<const std::byte> frame,
                            std::chrono::system_clock::time_point timestamp) = 0;
};

// Extension point for a future PCAPNG writer and Wireshark extcap control pipe.
class ExtcapProvider {
 public:
  virtual ~ExtcapProvider() = default;
  virtual std::string_view interface_name() const = 0;
  virtual void start(CaptureSink& sink) = 0;
  virtual void stop() = 0;
};

}  // namespace graphx
