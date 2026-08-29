#pragma once

#include "graphx/transport.hpp"

#include <atomic>

namespace graphx {

class TcpTransport final : public Transport {
 public:
  static TcpTransport connect(Endpoint endpoint, std::string edge_id = {},
                              TraceSink* trace_sink = nullptr);
  static TcpTransport listen(Endpoint endpoint, std::string edge_id = {},
                             TraceSink* trace_sink = nullptr);

  TcpTransport(TcpTransport&& other) noexcept;
  TcpTransport& operator=(TcpTransport&& other) noexcept;
  TcpTransport(const TcpTransport&) = delete;
  TcpTransport& operator=(const TcpTransport&) = delete;
  ~TcpTransport() override;

  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) override;
  void close() override;

 private:
  TcpTransport(int socket, std::string edge_id, TraceSink* trace_sink);
  int socket_{-1};
  std::string edge_id_;
  TraceSink* trace_sink_{};
  NullTraceSink null_trace_sink_;
};

}  // namespace graphx
