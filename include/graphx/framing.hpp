#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace graphx {

inline constexpr std::uint32_t kMaxFrameBytes = 16 * 1024 * 1024;
std::vector<std::byte> frame(std::span<const std::byte> payload);
std::uint32_t decode_frame_size(std::span<const std::byte, 4> prefix);

}  // namespace graphx
