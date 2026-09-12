#pragma once

#include "graphx/envelope.hpp"
#include "graphx/observability.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace graphx {

struct Endpoint {
  std::string host;
  std::uint16_t port{};
};

enum class ReceiveStatus { message, timeout, end_of_stream, cancelled };

struct ReceiveResult {
  ReceiveStatus status{ReceiveStatus::timeout};
  std::optional<Envelope> envelope;

  [[nodiscard]] bool has_message() const noexcept {
    return status == ReceiveStatus::message && envelope.has_value();
  }
};

[[nodiscard]] constexpr std::string_view to_string(ReceiveStatus status) noexcept {
  switch (status) {
    case ReceiveStatus::message:
      return "message";
    case ReceiveStatus::timeout:
      return "timeout";
    case ReceiveStatus::end_of_stream:
      return "end_of_stream";
    case ReceiveStatus::cancelled:
      return "cancelled";
  }
  return "unknown";
}

class Transport {
 public:
  virtual ~Transport() = default;
  virtual void send(const Envelope& envelope) = 0;
  virtual std::optional<Envelope> receive(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) {
    return std::move(receive_result(timeout).envelope);
  }
  virtual void close() = 0;
  virtual ReceiveResult receive_result(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) = 0;
};

using TransportPtr = std::unique_ptr<Transport>;

}  // namespace graphx
