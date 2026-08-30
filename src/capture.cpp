#include "graphx/capture.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace graphx {
namespace {

constexpr std::uint32_t kSectionHeaderBlock = 0x0a0d0d0a;
constexpr std::uint32_t kInterfaceDescriptionBlock = 0x00000001;
constexpr std::uint32_t kEnhancedPacketBlock = 0x00000006;
constexpr std::uint16_t kLinktypeUser0 = 147;

void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xff));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xff));
}

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8)
    out.push_back(static_cast<std::byte>((value >> shift) & 0xff));
}

void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8)
    out.push_back(static_cast<std::byte>((value >> shift) & 0xff));
}

void put_bytes(std::vector<std::byte>& out, std::string_view value) {
  for (const char byte : value) out.push_back(static_cast<std::byte>(byte));
}

void pad32(std::vector<std::byte>& out) {
  while (out.size() % 4 != 0) out.push_back(std::byte{});
}

void put_option(std::vector<std::byte>& out, std::uint16_t code, std::string_view value) {
  if (value.size() > UINT16_MAX) throw std::length_error("PCAPNG option is too large");
  put_u16(out, code);
  put_u16(out, static_cast<std::uint16_t>(value.size()));
  put_bytes(out, value);
  pad32(out);
}

void end_options(std::vector<std::byte>& out) {
  put_u16(out, 0);
  put_u16(out, 0);
}

std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += character;
    }
  }
  return escaped;
}

std::vector<std::byte> block(std::uint32_t type, std::vector<std::byte> body) {
  const auto total = static_cast<std::uint32_t>(body.size() + 12);
  std::vector<std::byte> output;
  output.reserve(total);
  put_u32(output, type);
  put_u32(output, total);
  output.insert(output.end(), body.begin(), body.end());
  put_u32(output, total);
  return output;
}

void write_all(std::ofstream& stream, std::span<const std::byte> bytes) {
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!stream) throw std::runtime_error("failed to write PCAPNG capture");
}

}  // namespace

struct PcapngCaptureSink::Impl {
  explicit Impl(std::filesystem::path output, std::uint32_t maximum)
      : path(std::move(output)), snaplen(maximum) {
    if (snaplen == 0) throw std::invalid_argument("PCAPNG snaplen must be positive");
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    stream.open(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot open PCAPNG capture: " + path.string());

    std::vector<std::byte> section;
    put_u32(section, 0x1a2b3c4d);
    put_u16(section, 1);
    put_u16(section, 0);
    put_u64(section, UINT64_MAX);
    put_option(section, 1,
               "GraphX canonical application frames: u32be length + GXE envelope; LINKTYPE_USER0");
    end_options(section);
    write_all(stream, block(kSectionHeaderBlock, std::move(section)));

    std::vector<std::byte> interface;
    put_u16(interface, kLinktypeUser0);
    put_u16(interface, 0);
    put_u32(interface, snaplen);
    put_option(interface, 2, "graphx-framed-envelope");
    put_option(interface, 3,
               "GraphX application frame, not an Ethernet or IP network packet");
    put_u16(interface, 9);
    put_u16(interface, 1);
    interface.push_back(std::byte{9});  // 10^-9 second timestamp resolution.
    pad32(interface);
    end_options(interface);
    write_all(stream, block(kInterfaceDescriptionBlock, std::move(interface)));
    stream.flush();
  }

  std::filesystem::path path;
  std::uint32_t snaplen;
  std::ofstream stream;
  mutable std::mutex mutex;
  std::uint64_t packets{};
  std::uint64_t last_offset{};
};

PcapngCaptureSink::PcapngCaptureSink(std::filesystem::path path, std::uint32_t snaplen)
    : impl_(std::make_unique<Impl>(std::move(path), snaplen)) {}

PcapngCaptureSink::~PcapngCaptureSink() = default;

void PcapngCaptureSink::record_frame(std::string_view edge_id,
                                     std::span<const std::byte> frame,
                                     std::chrono::system_clock::time_point timestamp,
                                     const Metadata& metadata) {
  std::scoped_lock lock(impl_->mutex);
  const auto captured = static_cast<std::uint32_t>(
      std::min<std::size_t>(frame.size(), impl_->snaplen));
  const auto original = static_cast<std::uint32_t>(
      std::min<std::size_t>(frame.size(), UINT32_MAX));
  const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
      timestamp.time_since_epoch()).count();
  const auto tick = static_cast<std::uint64_t>(std::max<std::int64_t>(0, nanoseconds));

  std::vector<std::byte> packet;
  put_u32(packet, 0);  // Interface ID.
  put_u32(packet, static_cast<std::uint32_t>(tick >> 32));
  put_u32(packet, static_cast<std::uint32_t>(tick));
  put_u32(packet, captured);
  put_u32(packet, original);
  packet.insert(packet.end(), frame.begin(), frame.begin() + captured);
  pad32(packet);

  const auto direction = metadata.direction == Direction::sent ? "sent" : "received";
  const auto comment = std::string{"{\"graphx\":{\"edge\":\""} + escape_json(edge_id) +
      "\",\"direction\":\"" + direction + "\",\"sequence\":" +
      std::to_string(metadata.sequence) + ",\"trace_id\":\"" +
      escape_json(metadata.trace_id) + "\",\"type\":\"" + escape_json(metadata.type) + "\"}}";
  put_option(packet, 1, comment);
  end_options(packet);
  const auto position = impl_->stream.tellp();
  if (position < 0) throw std::runtime_error("failed to locate PCAPNG packet offset");
  impl_->last_offset = static_cast<std::uint64_t>(position);
  write_all(impl_->stream, block(kEnhancedPacketBlock, std::move(packet)));
  impl_->stream.flush();
  ++impl_->packets;
}

const std::filesystem::path& PcapngCaptureSink::path() const noexcept { return impl_->path; }

std::uint64_t PcapngCaptureSink::packet_count() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->packets;
}

std::uint64_t PcapngCaptureSink::last_packet_offset() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->last_offset;
}

}  // namespace graphx
