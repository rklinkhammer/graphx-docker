#pragma once

#include "graphx/transport.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace graphx {

enum class SharedMemoryBackpressure { block, reject };

struct SharedMemoryOptions {
  std::size_t capacity{64};
  std::size_t max_message_bytes{1024 * 1024};
  SharedMemoryBackpressure backpressure{SharedMemoryBackpressure::block};
  std::chrono::milliseconds send_timeout{5000};
  std::chrono::milliseconds connect_timeout{5000};
};

// A bounded, single-producer/single-consumer POSIX shared-memory ring. The
// listener creates and owns the segment; the connector opens it.
class SharedMemoryTransport final : public Transport {
 public:
  static SharedMemoryTransport connect(std::string segment, std::string edge_id = {},
                                       TraceSink* trace_sink = nullptr,
                                       SharedMemoryOptions options = {});
  static SharedMemoryTransport listen(std::string segment, std::string edge_id = {},
                                      TraceSink* trace_sink = nullptr,
                                      SharedMemoryOptions options = {});

  SharedMemoryTransport(SharedMemoryTransport&&) noexcept;
  SharedMemoryTransport& operator=(SharedMemoryTransport&&) noexcept;
  SharedMemoryTransport(const SharedMemoryTransport&) = delete;
  SharedMemoryTransport& operator=(const SharedMemoryTransport&) = delete;
  ~SharedMemoryTransport() override;

  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                      -1}) override {
    return std::move(receive_result(timeout).envelope);
  }
  ReceiveResult receive_result(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   -1}) override;
  void close() override;

 private:
  struct Impl;
  static std::unique_ptr<Impl> create_impl(std::string segment, std::string edge_id,
                                           TraceSink* trace_sink,
                                           SharedMemoryOptions options, bool owner);
  explicit SharedMemoryTransport(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view to_string(SharedMemoryBackpressure policy) noexcept;

}  // namespace graphx
