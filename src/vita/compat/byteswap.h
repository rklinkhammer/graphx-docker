#pragma once
// macOS adapter for the three GNU byte-swap primitives used by pinned vrtgen.
#include <cstdint>
inline constexpr std::uint16_t bswap_16(std::uint16_t value) { return __builtin_bswap16(value); }
inline constexpr std::uint32_t bswap_32(std::uint32_t value) { return __builtin_bswap32(value); }
inline constexpr std::uint64_t bswap_64(std::uint64_t value) { return __builtin_bswap64(value); }
