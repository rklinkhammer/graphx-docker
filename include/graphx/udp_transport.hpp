#pragma once

#include "graphx/config.hpp"
#include "graphx/transport.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace graphx {

inline constexpr std::uint32_t kMaxUdpDatagramBytes = 65507;

struct UdpOptions {
  UdpMode mode{UdpMode::unicast};
  std::string interface;
  std::uint8_t ttl{1};
  bool loopback{true};
  bool reuse_address{};
  std::uint32_t receive_buffer_bytes{4 * 1024 * 1024};
  std::uint32_t send_buffer_bytes{4 * 1024 * 1024};
  std::uint32_t max_datagram_bytes{kMaxUdpDatagramBytes};
};

// IPv4 datagram transport. Each datagram contains exactly one u32be-framed
// GraphX envelope. UDP provides no delivery, ordering, or peer-liveness
// guarantee; connect() names the sending role rather than a session.
class UdpTransport final : public Transport {
 public:
  struct Impl;

  static UdpTransport connect(Endpoint destination, std::string bind_address,
                              std::string edge_id = {}, TraceSink* trace_sink = nullptr,
                              UdpOptions options = {});
  static UdpTransport listen(Endpoint bind, std::string destination, std::string edge_id = {},
                             TraceSink* trace_sink = nullptr, UdpOptions options = {});

  UdpTransport(UdpTransport&&) noexcept;
  UdpTransport& operator=(UdpTransport&&) noexcept;
  UdpTransport(const UdpTransport&) = delete;
  UdpTransport& operator=(const UdpTransport&) = delete;
  ~UdpTransport() override;

  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                      -1}) override {
    return std::move(receive_result(timeout).envelope);
  }
  ReceiveResult receive_result(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   -1}) override;
  void close() override;

 private:
  explicit UdpTransport(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace graphx
