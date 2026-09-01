#include "graphx/envelope.hpp"
#include "graphx/framing.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::span(reinterpret_cast<const std::byte*>(data), size);
  try {
    const auto envelope = graphx::deserialize(bytes);
    const auto encoded = graphx::serialize(envelope);
    const auto round_trip = graphx::deserialize(encoded);
    if (encoded.size() > graphx::kMaxFrameBytes ||
        round_trip.wire_version != envelope.wire_version ||
        round_trip.sequence != envelope.sequence ||
        round_trip.timestamp_ns != envelope.timestamp_ns || round_trip.type != envelope.type ||
        round_trip.trace_id != envelope.trace_id || round_trip.message_id != envelope.message_id ||
        round_trip.parent_message_id != envelope.parent_message_id ||
        round_trip.attributes != envelope.attributes || round_trip.payload != envelope.payload)
      std::abort();
  } catch (...) {
    // Malformed input is expected; sanitizer failures and invariant violations are not.
  }
  return 0;
}
