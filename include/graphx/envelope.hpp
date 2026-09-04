#pragma once

#include "graphx/types.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace graphx {

inline constexpr std::uint8_t kEnvelopeWireVersion1 = 1;
inline constexpr std::uint8_t kEnvelopeWireVersion2 = 2;
inline constexpr std::uint8_t kCurrentEnvelopeWireVersion = kEnvelopeWireVersion2;
inline constexpr std::size_t kIdentityHexLength = 32;
inline constexpr std::uint32_t kMaxEnvelopeAttributes = 4096;

// GraphX identities are canonical lowercase, non-zero 128-bit hexadecimal
// values. They are correlation identifiers, not credentials or authorization
// tokens.
[[nodiscard]] bool is_canonical_identity(std::string_view value) noexcept;
[[nodiscard]] std::string generate_identity();

struct Envelope {
  std::uint64_t sequence{};
  std::int64_t timestamp_ns{};
  std::string type;
  std::string trace_id;
  std::unordered_map<std::string, std::string> attributes;
  Bytes payload;
  // Added by envelope wire version 2. Empty on decoded version-1 envelopes.
  std::string message_id;
  std::string parent_message_id;
  // Default 1 preserves source behavior for legacy aggregate initialization.
  // Envelope::make and Envelope::derive create current-version envelopes.
  std::uint8_t wire_version{kEnvelopeWireVersion1};

  static Envelope make(std::uint64_t sequence, std::string type, Bytes payload);
  static Envelope derive(const Envelope& parent, std::uint64_t sequence, std::string type,
                         Bytes payload);
};

std::vector<std::byte> serialize(const Envelope& envelope);
// Computes the serialized envelope size without allocating or copying payload
// data. Transports with a smaller wire limit use this before serialization.
std::size_t serialized_size(const Envelope& envelope);
Envelope deserialize(std::span<const std::byte> bytes);

}  // namespace graphx
