#pragma once

#include "graphx/envelope.hpp"
#include "graphx/observability.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace graphx {

struct Endpoint {
  std::string host;
  std::uint16_t port{};
};

class Transport {
 public:
  virtual ~Transport() = default;
  virtual void send(const Envelope& envelope) = 0;
  virtual std::optional<Envelope> receive(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) = 0;
  virtual void close() = 0;
};

using TransportPtr = std::unique_ptr<Transport>;

}  // namespace graphx
