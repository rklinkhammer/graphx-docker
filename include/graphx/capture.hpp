#pragma once

#include "graphx/observability.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace graphx {

// PCAPNG writer for canonical GraphX application frames. Packets use
// LINKTYPE_USER0 (147), never an Ethernet/IP link type, and carry correlation
// fields in the Enhanced Packet Block comment option.
class PcapngCaptureSink final : public CaptureSink {
 public:
  explicit PcapngCaptureSink(std::filesystem::path path,
                             std::uint32_t snaplen = 16 * 1024 * 1024 + 4,
                             std::uint64_t max_file_bytes = 256ULL * 1024 * 1024,
                             std::uint64_t max_packets = 1'000'000);
  ~PcapngCaptureSink() override;
  PcapngCaptureSink(const PcapngCaptureSink&) = delete;
  PcapngCaptureSink& operator=(const PcapngCaptureSink&) = delete;

  void record_frame(std::string_view edge_id, std::span<const std::byte> frame,
                    std::chrono::system_clock::time_point timestamp,
                    const Metadata& metadata) override;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::uint64_t packet_count() const noexcept;
  [[nodiscard]] std::uint64_t last_packet_offset() const noexcept;
  [[nodiscard]] std::uint64_t bytes_written() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Writer for actual IEEE 802.3/Ethernet frames. Unlike PcapngCaptureSink this
// uses LINKTYPE_ETHERNET (1), and callers must supply bytes beginning with a
// real Ethernet header captured from an L2 interface.
class EthernetPcapngCaptureSink final {
 public:
  EthernetPcapngCaptureSink(std::filesystem::path path, std::string interface_name,
                            std::uint32_t snaplen = 262144,
                            std::uint64_t max_file_bytes = 256ULL * 1024 * 1024,
                            std::uint64_t max_packets = 1'000'000);
  ~EthernetPcapngCaptureSink();
  EthernetPcapngCaptureSink(const EthernetPcapngCaptureSink&) = delete;
  EthernetPcapngCaptureSink& operator=(const EthernetPcapngCaptureSink&) = delete;

  void record_packet(std::span<const std::byte> ethernet_frame,
                     std::chrono::system_clock::time_point timestamp,
                     std::string_view comment = {});

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::uint64_t packet_count() const noexcept;
  [[nodiscard]] std::uint64_t last_packet_offset() const noexcept;
  [[nodiscard]] std::uint64_t bytes_written() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace graphx
