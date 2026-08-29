#pragma once

#include "graphx/types.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphx {

struct Envelope {
  std::uint64_t sequence{};
  std::int64_t timestamp_ns{};
  std::string type;
  std::string trace_id;
  std::unordered_map<std::string, std::string> attributes;
  Bytes payload;

  static Envelope make(std::uint64_t sequence, std::string type, Bytes payload);
};

std::vector<std::byte> serialize(const Envelope& envelope);
Envelope deserialize(std::span<const std::byte> bytes);

}  // namespace graphx
