#include "graphx/envelope.hpp"
#include "graphx/framing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::span(reinterpret_cast<const std::byte*>(data), size);

  if (size <= graphx::kMaxFrameBytes) {
    const auto encoded = graphx::frame(bytes);
    const auto prefix = std::span<const std::byte, 4>(encoded.data(), 4);
    if (encoded.size() != size + 4 || graphx::decode_frame_size(prefix) != size) std::abort();
  }

  if (size < 4) return 0;
  std::array<std::byte, 4> prefix{};
  for (std::size_t index = 0; index < prefix.size(); ++index) prefix[index] = bytes[index];
  try {
    const auto payload_size = graphx::decode_frame_size(prefix);
    if (payload_size <= size - 4) {
      try {
        const auto envelope = graphx::deserialize(bytes.subspan(4, payload_size));
        const auto round_trip = graphx::serialize(envelope);
        if (round_trip.size() > graphx::kMaxFrameBytes) std::abort();
      } catch (...) {
        // A valid frame can contain an invalid envelope.
      }
    }
  } catch (...) {
    // Invalid frame prefixes are expected fuzz inputs.
  }
  return 0;
}
