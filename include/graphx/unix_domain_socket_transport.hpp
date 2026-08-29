#pragma once

#include "graphx/transport.hpp"

#include <string>

namespace graphx {

class UnixDomainSocketTransport final : public Transport {
 public:
  static UnixDomainSocketTransport connect(std::string path, std::string edge_id,
                                            TraceSink* trace_sink = nullptr);
  static UnixDomainSocketTransport listen(std::string path, std::string edge_id,
                                           TraceSink* trace_sink = nullptr);

  UnixDomainSocketTransport(const UnixDomainSocketTransport&) = delete;
  UnixDomainSocketTransport& operator=(const UnixDomainSocketTransport&) = delete;
  UnixDomainSocketTransport(UnixDomainSocketTransport&& other) noexcept;
  UnixDomainSocketTransport& operator=(UnixDomainSocketTransport&& other) noexcept;
  ~UnixDomainSocketTransport() override;

  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(std::chrono::milliseconds timeout) override;
  void close() override;

 private:
  UnixDomainSocketTransport(int socket, std::string edge_id, TraceSink* trace_sink,
                            std::string owned_path = {});
  int socket_{-1};
  std::string edge_id_;
  TraceSink* trace_sink_{};
  NullTraceSink null_trace_sink_;
  std::string owned_path_;
};

}  // namespace graphx
