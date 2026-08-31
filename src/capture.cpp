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
constexpr std::uint16_t kLinktypeEthernet = 1;
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

class WriterState {
 public:
  WriterState(std::filesystem::path output, std::uint32_t maximum,
              std::uint16_t linktype, std::string_view interface_name,
              std::string_view interface_description, std::string_view section_comment)
      : path_(std::move(output)), snaplen_(maximum) {
    if (snaplen_ == 0) throw std::invalid_argument("PCAPNG snaplen must be positive");
    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
    stream_.open(path_, std::ios::binary | std::ios::trunc);
    if (!stream_) throw std::runtime_error("cannot open PCAPNG capture: " + path_.string());

    std::vector<std::byte> section;
    put_u32(section, 0x1a2b3c4d);
    put_u16(section, 1);
    put_u16(section, 0);
    put_u64(section, UINT64_MAX);
    put_option(section, 1, section_comment);
    end_options(section);
    write_all(stream_, block(kSectionHeaderBlock, std::move(section)));

    std::vector<std::byte> interface;
    put_u16(interface, linktype);
    put_u16(interface, 0);
    put_u32(interface, snaplen_);
    put_option(interface, 2, interface_name);
    put_option(interface, 3, interface_description);
    put_u16(interface, 9);
    put_u16(interface, 1);
    interface.push_back(std::byte{9});  // 10^-9 second timestamp resolution.
    pad32(interface);
    end_options(interface);
    write_all(stream_, block(kInterfaceDescriptionBlock, std::move(interface)));
    stream_.flush();
  }

  void record(std::span<const std::byte> bytes,
              std::chrono::system_clock::time_point timestamp,
              std::string_view comment) {
    std::scoped_lock lock(mutex_);
    const auto captured = static_cast<std::uint32_t>(
        std::min<std::size_t>(bytes.size(), snaplen_));
    const auto original = static_cast<std::uint32_t>(
        std::min<std::size_t>(bytes.size(), UINT32_MAX));
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
        timestamp.time_since_epoch()).count();
    const auto tick = static_cast<std::uint64_t>(std::max<std::int64_t>(0, nanoseconds));

    std::vector<std::byte> packet;
    put_u32(packet, 0);
    put_u32(packet, static_cast<std::uint32_t>(tick >> 32));
    put_u32(packet, static_cast<std::uint32_t>(tick));
    put_u32(packet, captured);
    put_u32(packet, original);
    packet.insert(packet.end(), bytes.begin(), bytes.begin() + captured);
    pad32(packet);
    if (!comment.empty()) put_option(packet, 1, comment);
    end_options(packet);
    const auto position = stream_.tellp();
    if (position < 0) throw std::runtime_error("failed to locate PCAPNG packet offset");
    last_offset_ = static_cast<std::uint64_t>(position);
    write_all(stream_, block(kEnhancedPacketBlock, std::move(packet)));
    stream_.flush();
    ++packets_;
  }

  const std::filesystem::path& path() const noexcept { return path_; }
  std::uint64_t packet_count() const noexcept {
    std::scoped_lock lock(mutex_);
    return packets_;
  }
  std::uint64_t last_packet_offset() const noexcept {
    std::scoped_lock lock(mutex_);
    return last_offset_;
  }

 private:
  std::filesystem::path path_;
  std::uint32_t snaplen_;
  std::ofstream stream_;
  mutable std::mutex mutex_;
  std::uint64_t packets_{};
  std::uint64_t last_offset_{};
};

struct PcapngCaptureSink::Impl {
  explicit Impl(std::filesystem::path output, std::uint32_t maximum)
      : writer(std::move(output), maximum, kLinktypeUser0,
               "graphx-framed-envelope",
               "GraphX application frame, not an Ethernet or IP network packet",
               "GraphX canonical application frames: u32be length + GXE envelope; LINKTYPE_USER0") {}
  WriterState writer;
};

PcapngCaptureSink::PcapngCaptureSink(std::filesystem::path path, std::uint32_t snaplen)
    : impl_(std::make_unique<Impl>(std::move(path), snaplen)) {}

PcapngCaptureSink::~PcapngCaptureSink() = default;

void PcapngCaptureSink::record_frame(std::string_view edge_id,
                                     std::span<const std::byte> frame,
                                     std::chrono::system_clock::time_point timestamp,
                                     const Metadata& metadata) {
  const auto direction = metadata.direction == Direction::sent ? "sent" : "received";
  const auto comment = std::string{"{\"graphx\":{\"edge\":\""} + escape_json(edge_id) +
      "\",\"direction\":\"" + direction + "\",\"sequence\":" +
      std::to_string(metadata.sequence) + ",\"trace_id\":\"" +
      escape_json(metadata.trace_id) + "\",\"type\":\"" + escape_json(metadata.type) + "\"}}";
  impl_->writer.record(frame, timestamp, comment);
}

const std::filesystem::path& PcapngCaptureSink::path() const noexcept {
  return impl_->writer.path();
}

std::uint64_t PcapngCaptureSink::packet_count() const noexcept {
  return impl_->writer.packet_count();
}

std::uint64_t PcapngCaptureSink::last_packet_offset() const noexcept {
  return impl_->writer.last_packet_offset();
}

struct EthernetPcapngCaptureSink::Impl {
  Impl(std::filesystem::path output, std::string interface_name, std::uint32_t maximum)
      : writer(std::move(output), maximum, kLinktypeEthernet, interface_name,
               "IEEE 802.3/Ethernet frames captured from " + interface_name,
               "GraphX standard Ethernet packet capture; LINKTYPE_ETHERNET") {}
  WriterState writer;
};

EthernetPcapngCaptureSink::EthernetPcapngCaptureSink(std::filesystem::path path,
                                                     std::string interface_name,
                                                     std::uint32_t snaplen)
    : impl_(std::make_unique<Impl>(std::move(path), std::move(interface_name), snaplen)) {}

EthernetPcapngCaptureSink::~EthernetPcapngCaptureSink() = default;

void EthernetPcapngCaptureSink::record_packet(
    std::span<const std::byte> ethernet_frame,
    std::chrono::system_clock::time_point timestamp, std::string_view comment) {
  if (ethernet_frame.size() < 14)
    throw std::invalid_argument("Ethernet frame must include a 14-byte header");
  impl_->writer.record(ethernet_frame, timestamp, comment);
}

const std::filesystem::path& EthernetPcapngCaptureSink::path() const noexcept {
  return impl_->writer.path();
}

std::uint64_t EthernetPcapngCaptureSink::packet_count() const noexcept {
  return impl_->writer.packet_count();
}

std::uint64_t EthernetPcapngCaptureSink::last_packet_offset() const noexcept {
  return impl_->writer.last_packet_offset();
}

}  // namespace graphx
