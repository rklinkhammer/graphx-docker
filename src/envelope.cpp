#include "graphx/envelope.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace graphx {
namespace {

template <typename T>
void put(std::vector<std::byte>& out, T value) {
  for (int shift = static_cast<int>(sizeof(T) - 1) * 8; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> shift) & 0xff));
  }
}

void put_string(std::vector<std::byte>& out, std::string_view value) {
  if (value.size() > UINT32_MAX) throw std::length_error("string too large");
  put<std::uint32_t>(out, static_cast<std::uint32_t>(value.size()));
  for (char c : value) out.push_back(static_cast<std::byte>(c));
}

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template <typename T> T get() {
    require(sizeof(T));
    std::uint64_t value{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
      value = (value << 8) | std::to_integer<unsigned char>(bytes_[position_++]);
    return static_cast<T>(value);
  }

  std::string string() {
    auto size = get<std::uint32_t>();
    require(size);
    const auto* begin = reinterpret_cast<const char*>(bytes_.data() + position_);
    position_ += size;
    return {begin, size};
  }

  bool done() const { return position_ == bytes_.size(); }

 private:
  void require(std::size_t count) {
    if (count > bytes_.size() - position_) throw std::runtime_error("truncated envelope");
  }
  std::span<const std::byte> bytes_;
  std::size_t position_{};
};

}  // namespace

Envelope Envelope::make(std::uint64_t sequence, std::string type, Bytes payload) {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return {.sequence = sequence,
          .timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
          .type = std::move(type),
          .trace_id = "trace-" + std::to_string(sequence),
          .attributes = {},
          .payload = std::move(payload)};
}

std::vector<std::byte> serialize(const Envelope& envelope) {
  std::vector<std::byte> out;
  out.reserve(64 + envelope.payload.size());
  out.insert(out.end(), {std::byte{'G'}, std::byte{'X'}, std::byte{'E'}, std::byte{1}});
  put(out, envelope.sequence);
  put(out, static_cast<std::uint64_t>(envelope.timestamp_ns));
  put_string(out, envelope.type);
  put_string(out, envelope.trace_id);

  std::vector<std::pair<std::string, std::string>> attributes(envelope.attributes.begin(),
                                                              envelope.attributes.end());
  std::sort(attributes.begin(), attributes.end());
  put<std::uint32_t>(out, static_cast<std::uint32_t>(attributes.size()));
  for (const auto& [key, value] : attributes) {
    put_string(out, key);
    put_string(out, value);
  }
  put_string(out, envelope.payload);
  return out;
}

Envelope deserialize(std::span<const std::byte> bytes) {
  Reader reader(bytes);
  if (reader.get<std::uint8_t>() != 'G' || reader.get<std::uint8_t>() != 'X' ||
      reader.get<std::uint8_t>() != 'E' || reader.get<std::uint8_t>() != 1)
    throw std::runtime_error("invalid envelope magic or version");

  Envelope envelope;
  envelope.sequence = reader.get<std::uint64_t>();
  envelope.timestamp_ns = static_cast<std::int64_t>(reader.get<std::uint64_t>());
  envelope.type = reader.string();
  envelope.trace_id = reader.string();
  const auto attribute_count = reader.get<std::uint32_t>();
  if (attribute_count > 4096) throw std::runtime_error("too many envelope attributes");
  for (std::uint32_t i = 0; i < attribute_count; ++i) {
    auto key = reader.string();
    envelope.attributes.emplace(std::move(key), reader.string());
  }
  envelope.payload = reader.string();
  if (!reader.done()) throw std::runtime_error("trailing envelope bytes");
  return envelope;
}

}  // namespace graphx
