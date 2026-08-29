#include "graphx/framing.hpp"

#include <stdexcept>

namespace graphx {

std::vector<std::byte> frame(std::span<const std::byte> payload) {
  if (payload.size() > kMaxFrameBytes) throw std::length_error("frame too large");
  const auto size = static_cast<std::uint32_t>(payload.size());
  std::vector<std::byte> out;
  out.reserve(4 + payload.size());
  out.push_back(static_cast<std::byte>((size >> 24) & 0xff));
  out.push_back(static_cast<std::byte>((size >> 16) & 0xff));
  out.push_back(static_cast<std::byte>((size >> 8) & 0xff));
  out.push_back(static_cast<std::byte>(size & 0xff));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::uint32_t decode_frame_size(std::span<const std::byte, 4> prefix) {
  std::uint32_t size{};
  for (auto byte : prefix) size = (size << 8) | std::to_integer<std::uint8_t>(byte);
  if (size > kMaxFrameBytes) throw std::length_error("frame exceeds configured maximum");
  return size;
}

}  // namespace graphx
