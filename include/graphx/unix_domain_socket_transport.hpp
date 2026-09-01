#pragma once

#include "graphx/transport.hpp"

#include <atomic>
#include <string>

namespace graphx {

struct UnixDomainSocketOptions {
  std::chrono::milliseconds connect_timeout{5000};
  std::chrono::milliseconds send_timeout{5000};
};

class UnixDomainSocketTransport final : public Transport {
 public:
  static UnixDomainSocketTransport connect(std::string path, std::string edge_id,
                                           TraceSink* trace_sink = nullptr);
  static UnixDomainSocketTransport connect(std::string path, std::string edge_id,
                                           TraceSink* trace_sink, UnixDomainSocketOptions options);
  static UnixDomainSocketTransport listen(std::string path, std::string edge_id,
                                          TraceSink* trace_sink = nullptr);
  static UnixDomainSocketTransport listen(std::string path, std::string edge_id,
                                          TraceSink* trace_sink, UnixDomainSocketOptions options);

  UnixDomainSocketTransport(const UnixDomainSocketTransport&) = delete;
  UnixDomainSocketTransport& operator=(const UnixDomainSocketTransport&) = delete;
  UnixDomainSocketTransport(UnixDomainSocketTransport&& other) noexcept;
  UnixDomainSocketTransport& operator=(UnixDomainSocketTransport&& other) noexcept;
  ~UnixDomainSocketTransport() override;

  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                      -1}) override {
    return std::move(receive_result(timeout).envelope);
  }
  ReceiveResult receive_result(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   -1}) override;
  void close() override;

 private:
  UnixDomainSocketTransport(int socket, int listener, std::string edge_id, TraceSink* trace_sink,
                            UnixDomainSocketOptions options, std::string owned_path = {});
  void invalidate_connection() noexcept;
  void report_error(std::string_view message) noexcept;
  [[noreturn]] void fail_connection(std::string message);
  std::atomic<int> socket_{-1};
  std::atomic<int> listener_{-1};
  std::atomic<int> cancel_read_{-1};
  std::atomic<int> cancel_write_{-1};
  std::atomic<bool> closed_{false};
  std::string edge_id_;
  TraceSink* trace_sink_{};
  NullTraceSink null_trace_sink_;
  UnixDomainSocketOptions options_;
  std::string owned_path_;
};

}  // namespace graphx
