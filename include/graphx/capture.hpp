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
                             std::uint32_t snaplen = 16 * 1024 * 1024 + 4);
  ~PcapngCaptureSink() override;
  PcapngCaptureSink(const PcapngCaptureSink&) = delete;
  PcapngCaptureSink& operator=(const PcapngCaptureSink&) = delete;

  void record_frame(std::string_view edge_id, std::span<const std::byte> frame,
                    std::chrono::system_clock::time_point timestamp,
                    const Metadata& metadata) override;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::uint64_t packet_count() const noexcept;
  [[nodiscard]] std::uint64_t last_packet_offset() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace graphx
