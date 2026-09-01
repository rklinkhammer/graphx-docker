#pragma once

#include "graphx/transport.hpp"

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <mutex>

namespace graphx {

struct TcpRetryPolicy {
  std::size_t max_attempts{1};
  std::chrono::milliseconds initial_backoff{100};
  std::chrono::milliseconds max_backoff{2000};
};

struct TcpOptions {
  std::chrono::milliseconds connect_timeout{5000};
  std::chrono::milliseconds send_timeout{5000};
  TcpRetryPolicy retry;
  bool reconnect{false};
};

class TcpTransport final : public Transport {
 public:
  static TcpTransport connect(Endpoint endpoint, std::string edge_id = {},
                              TraceSink* trace_sink = nullptr, TcpOptions options = {});
  static TcpTransport listen(Endpoint endpoint, std::string edge_id = {},
                             TraceSink* trace_sink = nullptr, TcpOptions options = {});

  TcpTransport(TcpTransport&& other) noexcept;
  TcpTransport& operator=(TcpTransport&& other) noexcept;
  TcpTransport(const TcpTransport&) = delete;
  TcpTransport& operator=(const TcpTransport&) = delete;
  ~TcpTransport() override;

  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                      -1}) override {
    return std::move(receive_result(timeout).envelope);
  }
  ReceiveResult receive_result(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   -1}) override;
  void close() override;

 private:
  TcpTransport(int socket, int listener, Endpoint endpoint, std::string edge_id,
               TraceSink* trace_sink, TcpOptions options, bool outbound);
  void connect_outbound();
  bool accept_inbound(std::chrono::steady_clock::time_point deadline, bool has_deadline);
  void close_connection() noexcept;
  [[nodiscard]] std::string context(std::string_view action) const;

  std::atomic<int> socket_{-1};
  std::atomic<int> listener_{-1};
  std::atomic<bool> closed_{false};
  Endpoint endpoint_;
  std::string edge_id_;
  TraceSink* trace_sink_{};
  NullTraceSink null_trace_sink_;
  TcpOptions options_;
  bool outbound_{};
  bool ever_connected_{};
  std::mutex send_mutex_;
  std::mutex retry_mutex_;
  std::condition_variable retry_ready_;
};

}  // namespace graphx
