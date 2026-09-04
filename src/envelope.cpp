#include "graphx/envelope.hpp"

#include "graphx/framing.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <random>
#include <stdexcept>

namespace graphx {
namespace {

template <typename T>
void put(std::vector<std::byte>& out, T value) {
  for (int shift = static_cast<int>(sizeof(T) - 1) * 8; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> shift) & 0xff));
  }
}

std::uint64_t mix(std::uint64_t value) noexcept {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

std::uint64_t process_entropy() {
  std::random_device random;
  std::uint64_t value{};
  for (int index = 0; index < 4; ++index)
    value = (value << 16) ^ static_cast<std::uint64_t>(random());
  return mix(value ^ static_cast<std::uint64_t>(
                         std::chrono::steady_clock::now().time_since_epoch().count()));
}

char hex_digit(std::uint8_t value) noexcept {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

std::uint8_t hex_value(char value) {
  if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
  throw std::invalid_argument("identity must contain lowercase hexadecimal characters");
}

void put_identity(std::vector<std::byte>& out, std::string_view value, std::string_view field,
                  bool optional = false) {
  if (optional && value.empty()) {
    out.insert(out.end(), 16, std::byte{});
    return;
  }
  if (!is_canonical_identity(value))
    throw std::invalid_argument(std::string(field) +
                                " must be a non-zero 32-character lowercase hexadecimal identity");
  for (std::size_t index = 0; index < value.size(); index += 2)
    out.push_back(
        static_cast<std::byte>((hex_value(value[index]) << 4) | hex_value(value[index + 1])));
}

std::size_t checked_add(std::size_t total, std::size_t amount) {
  if (amount > kMaxFrameBytes || total > kMaxFrameBytes - amount)
    throw std::length_error("envelope exceeds 16777216-byte protocol maximum");
  return total + amount;
}

std::size_t checked_string_size(std::size_t total, std::size_t size) {
  total = checked_add(total, sizeof(std::uint32_t));
  return checked_add(total, size);
}

std::size_t encoded_size(const Envelope& envelope) {
  if (envelope.attributes.size() > kMaxEnvelopeAttributes)
    throw std::length_error("envelope has more than 4096 attributes");
  std::size_t total = envelope.wire_version == kEnvelopeWireVersion2 ? 72 : 24;
  total = checked_string_size(total, envelope.type.size());
  if (envelope.wire_version == kEnvelopeWireVersion1)
    total = checked_string_size(total, envelope.trace_id.size());
  for (const auto& [key, value] : envelope.attributes) {
    total = checked_string_size(total, key.size());
    total = checked_string_size(total, value.size());
  }
  return checked_string_size(total, envelope.payload.size());
}

void validate_serializable(const Envelope& envelope) {
  if (envelope.wire_version != kEnvelopeWireVersion1 &&
      envelope.wire_version != kEnvelopeWireVersion2)
    throw std::invalid_argument("unsupported envelope wire version " +
                                std::to_string(envelope.wire_version));
  if (envelope.wire_version == kEnvelopeWireVersion1 &&
      (!envelope.message_id.empty() || !envelope.parent_message_id.empty()))
    throw std::invalid_argument("envelope wire version 1 cannot encode message lineage identities");
}

void put_string(std::vector<std::byte>& out, std::string_view value) {
  if (value.size() > UINT32_MAX) throw std::length_error("string too large");
  put<std::uint32_t>(out, static_cast<std::uint32_t>(value.size()));
  for (char c : value) out.push_back(static_cast<std::byte>(c));
}

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template <typename T>
  T get() {
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

  std::string identity(bool optional = false) {
    require(16);
    bool zero = true;
    std::string value;
    value.reserve(kIdentityHexLength);
    for (std::size_t index = 0; index < 16; ++index) {
      const auto byte = std::to_integer<std::uint8_t>(bytes_[position_++]);
      zero = zero && byte == 0;
      value.push_back(hex_digit(byte >> 4));
      value.push_back(hex_digit(byte & 0x0f));
    }
    if (zero) {
      if (optional) return {};
      throw std::runtime_error("zero envelope identity is invalid");
    }
    return value;
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

bool is_canonical_identity(std::string_view value) noexcept {
  if (value.size() != kIdentityHexLength) return false;
  bool nonzero = false;
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
      return false;
    nonzero = nonzero || character != '0';
  }
  return nonzero;
}

std::string generate_identity() {
  static const std::uint64_t entropy = process_entropy();
  static std::atomic<std::uint64_t> counter{1};
  const auto ordinal = counter.fetch_add(1, std::memory_order_relaxed);
  const auto now =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::array words{mix(entropy ^ ordinal), mix(now ^ (ordinal << 1) ^ entropy)};
  std::string value;
  value.reserve(kIdentityHexLength);
  for (const auto word : words) {
    for (int shift = 60; shift >= 0; shift -= 4)
      value.push_back(hex_digit(static_cast<std::uint8_t>((word >> shift) & 0x0f)));
  }
  if (!is_canonical_identity(value)) value.back() = '1';
  return value;
}

Envelope Envelope::make(std::uint64_t sequence, std::string type, Bytes payload) {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return {.sequence = sequence,
          .timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count(),
          .type = std::move(type),
          .trace_id = generate_identity(),
          .attributes = {},
          .payload = std::move(payload),
          .message_id = generate_identity(),
          .parent_message_id = {},
          .wire_version = kCurrentEnvelopeWireVersion};
}

Envelope Envelope::derive(const Envelope& parent, std::uint64_t sequence, std::string type,
                          Bytes payload) {
  auto envelope = make(sequence, std::move(type), std::move(payload));
  if (is_canonical_identity(parent.trace_id)) envelope.trace_id = parent.trace_id;
  if (is_canonical_identity(parent.message_id)) envelope.parent_message_id = parent.message_id;
  return envelope;
}

std::size_t serialized_size(const Envelope& envelope) {
  validate_serializable(envelope);
  return encoded_size(envelope);
}

std::vector<std::byte> serialize(const Envelope& envelope) {
  const auto size = serialized_size(envelope);
  std::vector<std::byte> out;
  out.reserve(size);
  out.push_back(std::byte{'G'});
  out.push_back(std::byte{'X'});
  out.push_back(std::byte{'E'});
  out.push_back(static_cast<std::byte>(envelope.wire_version));
  put(out, envelope.sequence);
  put(out, static_cast<std::uint64_t>(envelope.timestamp_ns));
  if (envelope.wire_version == kEnvelopeWireVersion2) {
    put_identity(out, envelope.message_id, "message_id");
    put_identity(out, envelope.trace_id, "trace_id");
    put_identity(out, envelope.parent_message_id, "parent_message_id", true);
  }
  put_string(out, envelope.type);
  if (envelope.wire_version == kEnvelopeWireVersion1) put_string(out, envelope.trace_id);

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
  if (bytes.size() > kMaxFrameBytes)
    throw std::length_error("envelope exceeds 16777216-byte protocol maximum");
  Reader reader(bytes);
  if (reader.get<std::uint8_t>() != 'G' || reader.get<std::uint8_t>() != 'X' ||
      reader.get<std::uint8_t>() != 'E')
    throw std::runtime_error("invalid envelope magic");
  const auto version = reader.get<std::uint8_t>();
  if (version != kEnvelopeWireVersion1 && version != kEnvelopeWireVersion2)
    throw std::runtime_error("unsupported envelope wire version " + std::to_string(version));

  Envelope envelope;
  envelope.wire_version = version;
  envelope.sequence = reader.get<std::uint64_t>();
  envelope.timestamp_ns = static_cast<std::int64_t>(reader.get<std::uint64_t>());
  if (version == kEnvelopeWireVersion2) {
    envelope.message_id = reader.identity();
    envelope.trace_id = reader.identity();
    envelope.parent_message_id = reader.identity(true);
  }
  envelope.type = reader.string();
  if (version == kEnvelopeWireVersion1) envelope.trace_id = reader.string();
  const auto attribute_count = reader.get<std::uint32_t>();
  if (attribute_count > kMaxEnvelopeAttributes)
    throw std::runtime_error("too many envelope attributes");
  for (std::uint32_t i = 0; i < attribute_count; ++i) {
    auto key = reader.string();
    auto value = reader.string();
    if (!envelope.attributes.emplace(std::move(key), std::move(value)).second)
      throw std::runtime_error("duplicate envelope attribute key");
  }
  envelope.payload = reader.string();
  if (!reader.done()) throw std::runtime_error("trailing envelope bytes");
  return envelope;
}

}  // namespace graphx
