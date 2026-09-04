#include "graphx/capture.hpp"
#include "graphx/envelope.hpp"
#include "graphx/framing.hpp"
#include "graphx/in_process_transport.hpp"
#include "graphx/observability.hpp"
#include "graphx/shared_memory_transport.hpp"
#include "graphx/tcp_transport.hpp"
#include "graphx/udp_transport.hpp"
#include "graphx/unix_domain_socket_transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <openssl/hmac.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unordered_set>
#include <utility>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Operation>
void expect_failure(Operation&& operation, std::string_view expected) {
  bool failed{};
  try {
    std::forward<Operation>(operation)();
  } catch (const std::exception& error) {
    failed = true;
    expect(std::string_view(error.what()).find(expected) != std::string_view::npos,
           "failure has actionable context");
  }
  expect(failed, "operation expected to fail returned successfully");
}

class ThrowOnCloseTraceSink final : public graphx::TraceSink {
 public:
  void on_send(std::string_view, const graphx::Envelope&, std::size_t) override {}
  void on_receive(std::string_view, const graphx::Envelope&, std::size_t,
                  std::chrono::nanoseconds) override {}
  void on_error(std::string_view, std::string_view) override {}
  void on_connection(std::string_view, graphx::ConnectionState state) override {
    if (state == graphx::ConnectionState::closed)
      throw std::runtime_error("deliberate close observer failure");
  }
};

class ThrowOnEveryTraceSink final : public graphx::TraceSink {
 public:
  void on_send(std::string_view, const graphx::Envelope&, std::size_t) override { fail(); }
  void on_receive(std::string_view, const graphx::Envelope&, std::size_t,
                  std::chrono::nanoseconds) override {
    fail();
  }
  void on_error(std::string_view, std::string_view) override { fail(); }
  void on_connection(std::string_view, graphx::ConnectionState) override { fail(); }
  void on_udp_event(std::string_view, graphx::UdpEvent, std::uint64_t) override { fail(); }

 private:
  [[noreturn]] static void fail() { throw std::runtime_error("deliberate observer failure"); }
};

class CountingErrorTraceSink final : public graphx::TraceSink {
 public:
  void on_send(std::string_view, const graphx::Envelope&, std::size_t) override {}
  void on_receive(std::string_view, const graphx::Envelope&, std::size_t,
                  std::chrono::nanoseconds) override {}
  void on_error(std::string_view, std::string_view) override { ++errors; }

  std::uint64_t errors{};
};

void framing() {
  const std::array payload{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  const auto framed = graphx::frame(payload);
  expect(framed.size() == 7, "framed size");
  expect(graphx::decode_frame_size(std::span<const std::byte, 4>(framed.data(), 4)) == 3,
         "frame prefix");
  expect(framed[4] == payload[0] && framed[6] == payload[2], "frame payload");
}

void envelope_round_trip() {
  auto input = graphx::Envelope::make(42, "Sample", "payload");
  input.timestamp_ns = 123456789;
  input.trace_id = "0123456789abcdef0123456789abcdef";
  input.message_id = "fedcba9876543210fedcba9876543210";
  input.attributes = {{"unit", "volts"}, {"sensor", "a"}};
  const auto output = graphx::deserialize(graphx::serialize(input));
  expect(output.sequence == input.sequence, "envelope sequence");
  expect(output.timestamp_ns == input.timestamp_ns, "envelope timestamp");
  expect(output.type == input.type && output.payload == input.payload, "envelope body");
  expect(output.attributes == input.attributes, "envelope attributes");
  expect(output.wire_version == graphx::kEnvelopeWireVersion2 &&
             output.message_id == input.message_id && output.trace_id == input.trace_id,
         "version-2 envelope identities");
}

std::vector<std::byte> golden_bytes(std::string_view filename) {
  const auto path = std::filesystem::path(GRAPHX_SOURCE_DIR) / "tests" / "fixtures" / filename;
  std::ifstream stream(path);
  std::string hex;
  stream >> hex;
  expect(stream.good() || stream.eof(), "read protocol golden fixture");
  expect(hex.size() % 2 == 0, "golden fixture has complete bytes");
  const auto nibble = [](char value) -> std::uint8_t {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    throw std::runtime_error("invalid golden fixture hexadecimal character");
  };
  std::vector<std::byte> bytes;
  bytes.reserve(hex.size() / 2);
  for (std::size_t index = 0; index < hex.size(); index += 2)
    bytes.push_back(static_cast<std::byte>((nibble(hex[index]) << 4) | nibble(hex[index + 1])));
  return bytes;
}

void envelope_protocol_golden_vectors() {
  const auto version1 = golden_bytes("envelope-v1.hex");
  const auto legacy = graphx::deserialize(version1);
  expect(legacy.wire_version == graphx::kEnvelopeWireVersion1 && legacy.sequence == 1 &&
             legacy.timestamp_ns == 2 && legacy.type == "T" && legacy.trace_id == "legacy" &&
             legacy.message_id.empty() && legacy.parent_message_id.empty() &&
             legacy.attributes.at("a") == "b" && legacy.payload == "p",
         "decode canonical version-1 golden vector");
  expect(graphx::serialize(legacy) == version1, "version-1 golden vector is byte-stable");

  const auto version2 = golden_bytes("envelope-v2.hex");
  const auto current = graphx::deserialize(version2);
  expect(current.wire_version == graphx::kEnvelopeWireVersion2 && current.sequence == 1 &&
             current.timestamp_ns == 2 && current.type == "T" &&
             current.message_id == "00112233445566778899aabbccddeeff" &&
             current.trace_id == "102132435465768798a9babcbddcedfe" &&
             current.parent_message_id.empty() && current.attributes.at("a") == "b" &&
             current.payload == "p",
         "decode canonical version-2 golden vector");
  expect(graphx::serialize(current) == version2, "version-2 golden vector is byte-stable");
}

void envelope_identity_semantics() {
  auto root = graphx::Envelope::make(1, "Root", "value");
  expect(root.wire_version == graphx::kCurrentEnvelopeWireVersion &&
             graphx::is_canonical_identity(root.message_id) &&
             graphx::is_canonical_identity(root.trace_id) && root.parent_message_id.empty(),
         "new envelope has canonical root identities");
  auto transformed = root;
  transformed.type = "Transformed";
  expect(transformed.message_id == root.message_id && transformed.trace_id == root.trace_id,
         "ordinary transformation preserves logical identity");
  auto child = graphx::Envelope::derive(root, 2, "Child", "derived");
  expect(child.message_id != root.message_id && child.trace_id == root.trace_id &&
             child.parent_message_id == root.message_id,
         "derived envelope records causal lineage");

  std::vector<std::future<std::vector<std::string>>> workers;
  for (int worker = 0; worker < 8; ++worker) {
    workers.push_back(std::async(std::launch::async, [] {
      std::vector<std::string> identities;
      for (int index = 0; index < 250; ++index) identities.push_back(graphx::generate_identity());
      return identities;
    }));
  }
  std::unordered_set<std::string> unique;
  for (auto& worker : workers) {
    for (auto& identity : worker.get()) {
      expect(graphx::is_canonical_identity(identity), "generated identity is canonical");
      unique.insert(std::move(identity));
    }
  }
  expect(unique.size() == 2000, "generated identities are unique in concurrent sample");
}

void envelope_protocol_rejects_invalid_input() {
  const auto version2 = golden_bytes("envelope-v2.hex");
  const std::array<std::size_t, 6> truncated_lengths{0, 3, 4, 19, 35, version2.size() - 1};
  for (const std::size_t length : truncated_lengths) {
    expect_failure([&] { graphx::deserialize(std::span(version2).first(length)); }, "truncated");
  }
  auto unknown = version2;
  unknown[3] = std::byte{3};
  expect_failure([&] { graphx::deserialize(unknown); }, "unsupported envelope wire version 3");
  auto trailing = version2;
  trailing.push_back(std::byte{});
  expect_failure([&] { graphx::deserialize(trailing); }, "trailing envelope bytes");
  auto zero_message = version2;
  for (std::size_t index = 20; index < 36; ++index) zero_message.at(index) = std::byte{};
  expect_failure([&] { graphx::deserialize(zero_message); }, "zero envelope identity");

  const auto append_u32 = [](std::vector<std::byte>& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<std::byte>((value >> shift) & 0xff));
  };
  const auto append_string = [&](std::vector<std::byte>& bytes, std::string_view value) {
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    for (const char character : value) bytes.push_back(static_cast<std::byte>(character));
  };
  auto duplicate = golden_bytes("envelope-v1.hex");
  duplicate.resize(35);
  append_u32(duplicate, 2);
  append_string(duplicate, "a");
  append_string(duplicate, "b");
  append_string(duplicate, "a");
  append_string(duplicate, "c");
  append_string(duplicate, "p");
  expect_failure([&] { graphx::deserialize(duplicate); }, "duplicate envelope attribute key");

  auto excessive = duplicate;
  excessive.resize(35);
  append_u32(excessive, graphx::kMaxEnvelopeAttributes + 1);
  expect_failure([&] { graphx::deserialize(excessive); }, "too many envelope attributes");

  auto invalid_identity = graphx::Envelope::make(3, "Invalid", "value");
  invalid_identity.message_id = std::string(graphx::kIdentityHexLength, '0');
  expect_failure([&] { graphx::serialize(invalid_identity); }, "message_id");
  invalid_identity.message_id = "ABCDEF0123456789ABCDEF0123456789";
  expect_failure([&] { graphx::serialize(invalid_identity); }, "message_id");
  invalid_identity.message_id = graphx::generate_identity();
  invalid_identity.wire_version = 3;
  expect_failure([&] { graphx::serialize(invalid_identity); },
                 "unsupported envelope wire version 3");

  auto lossy_legacy = graphx::deserialize(golden_bytes("envelope-v1.hex"));
  lossy_legacy.message_id = graphx::generate_identity();
  expect_failure([&] { graphx::serialize(lossy_legacy); }, "version 1 cannot encode");

  auto oversized = graphx::Envelope::make(4, "Large", std::string(graphx::kMaxFrameBytes, 'x'));
  expect_failure([&] { graphx::serialize(oversized); }, "protocol maximum");
}

void envelope_protocol_exhaustive_boundaries() {
  bool helper_rejected_normal_return{};
  try {
    expect_failure([] {}, "not applicable");
  } catch (const std::exception& error) {
    helper_rejected_normal_return =
        std::string_view(error.what()).find("expected to fail") != std::string_view::npos;
  }
  expect(helper_rejected_normal_return,
         "failure assertion rejects a callable that returns successfully");

  const auto version2 = golden_bytes("envelope-v2.hex");
  for (std::size_t length = 0; length < version2.size(); ++length) {
    expect_failure([&] { graphx::deserialize(std::span(version2).first(length)); }, "truncated");
  }

  auto exact = graphx::Envelope::make(5, "Boundary", "");
  const auto overhead = graphx::serialize(exact).size();
  expect(overhead < graphx::kMaxFrameBytes, "envelope boundary has payload capacity");
  exact.payload.resize(graphx::kMaxFrameBytes - overhead, 'x');
  const auto encoded = graphx::serialize(exact);
  expect(encoded.size() == graphx::kMaxFrameBytes, "exact maximum envelope size is accepted");
  expect(graphx::deserialize(encoded).payload == exact.payload,
         "exact maximum envelope round trip");
  exact.payload.push_back('x');
  expect_failure([&] { graphx::serialize(exact); }, "protocol maximum");

  const std::vector<std::byte> maximum_frame(graphx::kMaxFrameBytes, std::byte{0x5a});
  const auto framed = graphx::frame(maximum_frame);
  expect(framed.size() == graphx::kMaxFrameBytes + 4 &&
             graphx::decode_frame_size(std::span<const std::byte, 4>(framed.data(), 4)) ==
                 graphx::kMaxFrameBytes,
         "exact maximum frame size is accepted");
  auto oversized_frame = maximum_frame;
  oversized_frame.push_back(std::byte{});
  expect_failure([&] { graphx::frame(oversized_frame); }, "too large");
}

void legacy_transport_adapter() {
  class LegacyTransport final : public graphx::Transport {
   public:
    void send(const graphx::Envelope&) override {}
    std::optional<graphx::Envelope> receive(std::chrono::milliseconds) override {
      if (delivered_) return std::nullopt;
      delivered_ = true;
      return graphx::Envelope::make(1, "Legacy", "compatible");
    }
    void close() override {}

   private:
    bool delivered_{};
  } transport;

  expect(transport.receive_result(1ms).status == graphx::ReceiveStatus::message,
         "legacy transport message adapter");
  expect(transport.receive_result(1ms).status == graphx::ReceiveStatus::timeout,
         "legacy empty optional adapter");
}

std::uint16_t little_u16(std::span<const std::byte> bytes, std::size_t offset) {
  return std::to_integer<std::uint8_t>(bytes[offset]) |
         (std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8);
}

std::uint32_t little_u32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value{};
  for (int index = 3; index >= 0; --index)
    value = (value << 8) | std::to_integer<std::uint8_t>(bytes[offset + index]);
  return value;
}

void pcapng_capture() {
  const auto path = std::filesystem::temp_directory_path() /
                    ("graphx-capture-" + std::to_string(::getpid()) + ".pcapng");
  std::filesystem::remove(path);
  std::ofstream(path) << "stale-capture-data";
  auto envelope = graphx::Envelope::make(42, "Sample", "payload");
  envelope.trace_id = "0123456789abcdef0123456789abcdef";
  envelope.message_id = "fedcba9876543210fedcba9876543210";
  const auto framed = graphx::frame(graphx::serialize(envelope));
  const auto timestamp = std::chrono::system_clock::time_point{
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::nanoseconds{123456789})};
  {
    graphx::PcapngCaptureSink capture(path, 16 * 1024 * 1024 + 4, 1024 * 1024, 1);
    capture.record_frame("samples", framed, timestamp,
                         {.direction = graphx::CaptureSink::Direction::sent,
                          .sequence = envelope.sequence,
                          .wire_version = envelope.wire_version,
                          .message_id = envelope.message_id,
                          .parent_message_id = envelope.parent_message_id,
                          .trace_id = envelope.trace_id,
                          .type = envelope.type});
    expect(capture.packet_count() == 1 && capture.path() == path &&
               capture.last_packet_offset() > 0 && capture.bytes_written() > framed.size(),
           "PCAPNG capture state");
    expect_failure(
        [&] {
          capture.record_frame("samples", framed, timestamp,
                               {.direction = graphx::CaptureSink::Direction::sent});
        },
        "packet limit");
  }

  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> characters((std::istreambuf_iterator<char>(stream)), {});
  const auto* begin = reinterpret_cast<const std::byte*>(characters.data());
  const std::span<const std::byte> bytes(begin, characters.size());
  const std::string file_text(characters.begin(), characters.end());
  expect(bytes.size() > framed.size() + 80, "PCAPNG output size");
  expect(little_u32(bytes, 0) == 0x0a0d0d0a, "PCAPNG section header");
  expect(file_text.find("stale-capture-data") == std::string::npos,
         "validated single-link capture is replaced");
  const auto section_length = little_u32(bytes, 4);
  expect(little_u32(bytes, section_length) == 1, "PCAPNG interface block");
  expect(little_u16(bytes, section_length + 8) == 147, "PCAPNG USER0 link type");
  const auto interface_length = little_u32(bytes, section_length + 4);
  const auto packet_offset = section_length + interface_length;
  expect(little_u32(bytes, packet_offset) == 6, "PCAPNG enhanced packet block");
  expect(little_u32(bytes, packet_offset + 20) == framed.size(), "PCAPNG captured length");
  expect(std::equal(framed.begin(), framed.end(), bytes.begin() + packet_offset + 28),
         "PCAPNG exact framed bytes");
  expect(
      file_text.find("\"edge\":\"samples\"") != std::string::npos &&
          file_text.find("\"direction\":\"sent\"") != std::string::npos &&
          file_text.find("\"sequence\":42") != std::string::npos &&
          file_text.find("\"message_id\":\"fedcba9876543210fedcba9876543210\"") !=
              std::string::npos &&
          file_text.find("\"trace_id\":\"0123456789abcdef0123456789abcdef\"") != std::string::npos,
      "PCAPNG correlation metadata");
  std::filesystem::remove(path);

  const auto link = path.string() + ".link";
  const auto target = path.string() + ".target";
  {
    std::ofstream(target) << "protected";
    std::filesystem::create_symlink(target, link);
    expect_failure([&] { graphx::PcapngCaptureSink rejected(link); }, "PCAPNG capture");
    std::ifstream protected_file(target);
    std::string protected_text;
    protected_file >> protected_text;
    expect(protected_text == "protected", "PCAPNG symlink target remains unchanged");
  }
  std::filesystem::remove(link);
  std::filesystem::remove(target);

  const auto hard_link = path.string() + ".hard-link";
  const auto hard_target = path.string() + ".hard-target";
  {
    std::ofstream(hard_target) << "protected-hard-link";
    std::filesystem::create_hard_link(hard_target, hard_link);
    expect_failure([&] { graphx::PcapngCaptureSink rejected(hard_link); }, "singly linked");
    std::ifstream protected_file(hard_target);
    std::string protected_text;
    protected_file >> protected_text;
    expect(protected_text == "protected-hard-link", "PCAPNG hard-link target remains unchanged");
  }
  std::filesystem::remove(hard_link);
  std::filesystem::remove(hard_target);

  const auto fifo = path.string() + ".fifo";
  expect(::mkfifo(fifo.c_str(), 0600) == 0, "create PCAPNG FIFO target");
  const auto fifo_start = std::chrono::steady_clock::now();
  expect_failure([&] { graphx::PcapngCaptureSink rejected(fifo); }, "PCAPNG capture");
  expect(std::chrono::steady_clock::now() - fifo_start < 1s,
         "PCAPNG FIFO target is rejected without blocking");
  std::filesystem::remove(fifo);

  const auto socket_path = path.string() + ".socket";
  const int socket_descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
  expect(socket_descriptor >= 0, "create PCAPNG Unix socket target");
  sockaddr_un socket_address{};
  socket_address.sun_family = AF_UNIX;
  std::memcpy(socket_address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  expect(::bind(socket_descriptor, reinterpret_cast<const sockaddr*>(&socket_address),
                sizeof(socket_address)) == 0,
         "bind PCAPNG Unix socket target");
  const auto socket_start = std::chrono::steady_clock::now();
  expect_failure([&] { graphx::PcapngCaptureSink rejected(socket_path); }, "PCAPNG capture");
  expect(std::chrono::steady_clock::now() - socket_start < 1s,
         "PCAPNG socket target is rejected without blocking");
  ::close(socket_descriptor);
  std::filesystem::remove(socket_path);

  expect_failure([] { graphx::PcapngCaptureSink rejected("/dev/null"); }, "regular file");

  const auto metadata_path = path.string() + ".metadata";
  {
    graphx::PcapngCaptureSink capture(metadata_path, 16 * 1024 * 1024 + 4, 1024 * 1024, 2);
    auto large_metadata = graphx::Envelope::make(43, std::string(70000, 'A'), "payload");
    const auto large_frame = graphx::frame(graphx::serialize(large_metadata));
    const graphx::CaptureSink::Metadata metadata{
        .direction = graphx::CaptureSink::Direction::sent,
        .sequence = large_metadata.sequence,
        .wire_version = large_metadata.wire_version,
        .message_id = large_metadata.message_id,
        .parent_message_id = large_metadata.parent_message_id,
        .trace_id = large_metadata.trace_id,
        .type = large_metadata.type};
    capture.record_frame("samples", large_frame, timestamp, metadata);
    capture.record_frame("samples", large_frame, timestamp, metadata);
    expect(capture.packet_count() == 2, "oversized optional metadata does not disable capture");
  }
  {
    std::ifstream stream(metadata_path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), {});
    expect(contents.find("\"metadata_truncated\":true") != std::string::npos,
           "oversized capture metadata has an explicit truncation marker");
  }
  std::filesystem::remove(metadata_path);

  const auto limited_path = path.string() + ".limited";
  {
    graphx::PcapngCaptureSink limited(limited_path, 16 * 1024 * 1024 + 4, 65536, 10);
    const std::vector<std::byte> large_frame(65536, std::byte{0x01});
    expect_failure(
        [&] {
          limited.record_frame("samples", large_frame, timestamp,
                               {.direction = graphx::CaptureSink::Direction::received});
        },
        "file size limit");
    expect(limited.packet_count() == 0 && limited.bytes_written() < 65536,
           "PCAPNG byte limit leaves a valid header and no partial packet");
  }
  std::filesystem::remove(limited_path);
}

void ethernet_pcapng_capture() {
  const auto path = std::filesystem::temp_directory_path() /
                    ("graphx-ethernet-" + std::to_string(::getpid()) + ".pcapng");
  std::filesystem::remove(path);
  const std::array frame{std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                         std::byte{0x00}, std::byte{0x02}, std::byte{0x02}, std::byte{0x00},
                         std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
                         std::byte{0x08}, std::byte{0x00}, std::byte{0x45}, std::byte{0x00},
                         std::byte{0x00}, std::byte{0x14}, std::byte{0x00}, std::byte{0x00}};
  {
    graphx::EthernetPcapngCaptureSink capture(path, "br-gx-mac");
    capture.record_packet(frame, std::chrono::system_clock::now(), "OVS mirror br-gx-mac");
    expect(capture.packet_count() == 1 && capture.last_packet_offset() > 0 &&
               capture.bytes_written() >= frame.size(),
           "Ethernet PCAPNG capture state");
    bool rejected{};
    try {
      capture.record_packet(std::span<const std::byte>(frame).first(13),
                            std::chrono::system_clock::now());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    expect(rejected, "Ethernet capture rejects headerless data");
  }

  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> characters((std::istreambuf_iterator<char>(stream)), {});
  const auto* begin = reinterpret_cast<const std::byte*>(characters.data());
  const std::span<const std::byte> bytes(begin, characters.size());
  const auto section_length = little_u32(bytes, 4);
  expect(little_u16(bytes, section_length + 8) == 1, "PCAPNG Ethernet link type");
  const auto interface_length = little_u32(bytes, section_length + 4);
  const auto packet_offset = section_length + interface_length;
  expect(little_u32(bytes, packet_offset) == 6, "Ethernet enhanced packet block");
  expect(little_u32(bytes, packet_offset + 20) == frame.size(), "Ethernet PCAPNG captured length");
  expect(std::equal(frame.begin(), frame.end(), bytes.begin() + packet_offset + 28),
         "Ethernet PCAPNG exact frame bytes");
  const std::string file_text(characters.begin(), characters.end());
  expect(file_text.find("IEEE 802.3/Ethernet") != std::string::npos &&
             file_text.find("OVS mirror br-gx-mac") != std::string::npos,
         "Ethernet PCAPNG interface and packet metadata");
  std::filesystem::remove(path);
}

void in_process() {
  auto channel = std::make_shared<graphx::InProcessChannel>();
  graphx::MetricsTraceSink metrics;
  graphx::InProcessTransport sender(channel, "local", &metrics),
      receiver(channel, "local", &metrics);
  sender.send(graphx::Envelope::make(7, "Ping", "hello"));
  const auto message = receiver.receive(std::chrono::milliseconds(10));
  expect(message && message->payload == "hello", "in-process delivery");
  const auto measured = metrics.edge("local");
  expect(measured.sent == 1 && measured.received == 1 && measured.sent_wire_bytes > 0 &&
             measured.received_wire_bytes > 0 &&
             measured.connection == graphx::ConnectionState::connected,
         "in-process tracing parity");
}

void in_process_typed_outcomes_and_backpressure() {
  graphx::InProcessOptions options;
  options.capacity = 1;
  options.backpressure = graphx::InProcessBackpressure::reject;
  auto channel = std::make_shared<graphx::InProcessChannel>(options);
  graphx::InProcessTransport sender(channel, "bounded"), receiver(channel, "bounded");

  expect(receiver.receive_result(5ms).status == graphx::ReceiveStatus::timeout,
         "in-process timeout is distinct");
  sender.send(graphx::Envelope::make(1, "Bounded", "first"));
  try {
    sender.send(graphx::Envelope::make(2, "Bounded", "second"));
    throw std::runtime_error("bounded in-process queue accepted overflow");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("backpressure=reject") != std::string_view::npos,
           "in-process reject policy");
  }
  const auto message = receiver.receive_result(20ms);
  expect(message.status == graphx::ReceiveStatus::message && message.envelope->sequence == 1,
         "typed in-process message outcome");
  sender.close();
  expect(receiver.receive_result(20ms).status == graphx::ReceiveStatus::end_of_stream,
         "in-process peer closure is end-of-stream");
  receiver.close();
  expect(receiver.receive_result(20ms).status == graphx::ReceiveStatus::cancelled,
         "in-process local closure is cancellation");

  options.backpressure = graphx::InProcessBackpressure::block;
  options.send_timeout = 1s;
  auto blocking_channel = std::make_shared<graphx::InProcessChannel>(options);
  graphx::InProcessTransport blocking_sender(blocking_channel, "blocking"),
      blocking_receiver(blocking_channel, "blocking");
  blocking_sender.send(graphx::Envelope::make(3, "Bounded", "queued"));
  auto blocked = std::async(std::launch::async, [&] {
    try {
      blocking_sender.send(graphx::Envelope::make(4, "Bounded", "blocked"));
    } catch (const std::exception&) {
      return true;
    }
    return false;
  });
  expect(blocked.wait_for(20ms) == std::future_status::timeout,
         "in-process block policy waits for capacity");
  blocking_sender.close();
  expect(blocked.wait_for(1s) == std::future_status::ready && blocked.get(),
         "in-process close cancels a blocked sender");
  expect(blocking_receiver.receive_result(20ms).status == graphx::ReceiveStatus::message,
         "in-process close drains committed messages");
  expect(blocking_receiver.receive_result(20ms).status == graphx::ReceiveStatus::end_of_stream,
         "in-process close ends stream after drain");
}

void in_process_validation_is_atomic() {
  graphx::InProcessOptions options;
  options.capacity = 1;
  options.backpressure = graphx::InProcessBackpressure::reject;
  auto channel = std::make_shared<graphx::InProcessChannel>(options);
  graphx::MetricsTraceSink metrics;
  graphx::InProcessTransport sender(channel, "validated", &metrics),
      receiver(channel, "validated", &metrics);

  const auto reject_without_publishing = [&](const graphx::Envelope& invalid,
                                             std::string_view expected_error) {
    try {
      sender.send(invalid);
      throw std::runtime_error("invalid in-process envelope was accepted");
    } catch (const std::exception& error) {
      expect(std::string_view(error.what()).find(expected_error) != std::string_view::npos,
             "in-process validation diagnostic");
    }
    expect(receiver.receive_result(2ms).status == graphx::ReceiveStatus::timeout,
           "failed in-process send leaves queue empty");
    expect(metrics.edge("validated").sent == 0,
           "failed in-process send does not emit send telemetry");
  };

  auto invalid_identity = graphx::Envelope::make(1, "Invalid", "identity");
  invalid_identity.message_id = std::string(graphx::kIdentityHexLength, '0');
  reject_without_publishing(invalid_identity, "message_id");

  auto lossy_v1 = graphx::Envelope::make(2, "Invalid", "lineage");
  lossy_v1.wire_version = graphx::kEnvelopeWireVersion1;
  reject_without_publishing(lossy_v1, "cannot encode message lineage");

  auto oversized = graphx::Envelope::make(3, "Invalid", "oversized");
  oversized.payload.resize(graphx::kMaxFrameBytes, 'x');
  reject_without_publishing(oversized, "protocol maximum");

  const auto valid = graphx::Envelope::make(4, "Valid", "published");
  sender.send(valid);
  const auto received = receiver.receive_result(20ms);
  expect(received.status == graphx::ReceiveStatus::message &&
             received.envelope->message_id == valid.message_id,
         "valid send succeeds after validation failures");
  expect(metrics.edge("validated").sent == 1, "only committed in-process send emits telemetry");
}

void metrics_sink() {
  graphx::MetricsTraceSink metrics;
  const auto envelope = graphx::Envelope::make(8, "Metric", "value");
  metrics.on_send("metrics-edge", envelope, 64);
  metrics.on_receive("metrics-edge", envelope, 64, std::chrono::microseconds(25));
  metrics.on_error("metrics-edge", "example");
  metrics.on_connection("metrics-edge", graphx::ConnectionState::connected);
  metrics.on_reconnect("metrics-edge");
  metrics.on_backpressure("metrics-edge", std::chrono::microseconds(40), false);
  metrics.on_backpressure("metrics-edge", {}, true);
  const auto edge = metrics.edge("metrics-edge");
  expect(edge.sent == 1 && edge.received == 1, "metrics message counters");
  expect(edge.sent_wire_bytes == 64 && edge.received_wire_bytes == 64 && edge.errors == 1,
         "metrics directional byte/error counters");
  expect(edge.total_latency == std::chrono::microseconds(25), "metrics latency");
  expect(edge.latency_buckets[1] == 1, "metrics latency histogram");
  expect(edge.connection == graphx::ConnectionState::connected && edge.reconnects == 1,
         "metrics connection state");
  expect(edge.backpressure_events == 2 && edge.rejected == 1 &&
             edge.total_backpressure == std::chrono::microseconds(40),
         "metrics backpressure");
}

void udp_runtime_control() {
  const int collector = ::socket(AF_INET, SOCK_DGRAM, 0);
  expect(collector >= 0, "UDP control collector socket");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  expect(::bind(collector, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
         "UDP control collector bind");
  socklen_t address_size = sizeof(address);
  expect(::getsockname(collector, reinterpret_cast<sockaddr*>(&address), &address_size) == 0,
         "UDP control collector address");
  timeval timeout{1, 0};
  ::setsockopt(collector, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  {
    constexpr std::string_view secret = "runtime-control-secret-012345678901";
    graphx::UdpJsonTraceSink sink("generator", "127.0.0.1", ntohs(address.sin_port),
                                  std::string(secret));
    sink.on_heartbeat("generator", 1.0);
    std::array<char, 2048> event{};
    sockaddr_storage runtime{};
    socklen_t runtime_size = sizeof(runtime);
    expect(::recvfrom(collector, event.data(), event.size(), 0,
                      reinterpret_cast<sockaddr*>(&runtime), &runtime_size) > 0,
           "UDP control endpoint registration");
    auto envelope = graphx::Envelope::make(12, std::string{"Observed"} + char{1}, "value");
    envelope.message_id = "00112233445566778899aabbccddeeff";
    envelope.trace_id = "102132435465768798a9babcbddcedfe";
    sink.on_send("samples", envelope, 123);
    const auto event_size = ::recv(collector, event.data(), event.size(), 0);
    expect(event_size > 0, "UDP envelope event delivery");
    const std::string_view event_json(event.data(), static_cast<std::size_t>(event_size));
    expect(event_json.find("\"wireVersion\":2") != std::string_view::npos &&
               event_json.find("\"messageId\":\"00112233445566778899aabbccddeeff\"") !=
                   std::string_view::npos &&
               event_json.find("\"parentMessageId\":\"\"") != std::string_view::npos &&
               event_json.find("\"traceId\":\"102132435465768798a9babcbddcedfe\"") !=
                   std::string_view::npos &&
               event_json.find("\"type\":\"Observed\\u0001\"") != std::string_view::npos,
           "UDP event carries canonical protocol identities");
    std::uint64_t command_sequence{};
    const auto command = [&](std::string_view action, std::string_view target = "generator",
                             std::int64_t expiry_offset = 1000) {
      ++command_sequence;
      const auto command_id = std::string{"00000000-0000-4000-8000-"} + std::string(11, '0') +
                              std::to_string(command_sequence);
      const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
      const auto payload = std::string{"{\"kind\":\"control\",\"action\":\""} +
                           std::string(action) + "\",\"commandId\":\"" + command_id +
                           "\",\"targetNode\":\"" + std::string(target) +
                           "\",\"expiresAt\":" + std::to_string(timestamp + expiry_offset) + "}";
      const auto nonce = std::string(31, '0') + std::to_string(command_sequence);
      const auto signed_value = std::to_string(timestamp) + "." + nonce + "." + payload;
      std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
      unsigned int digest_size{};
      expect(HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
                  reinterpret_cast<const unsigned char*>(signed_value.data()), signed_value.size(),
                  digest.data(), &digest_size) != nullptr,
             "UDP runtime command HMAC");
      constexpr std::string_view digits = "0123456789abcdef";
      std::string signature;
      signature.reserve(digest_size * 2);
      for (unsigned int index = 0; index < digest_size; ++index) {
        signature += digits[digest[index] >> 4];
        signature += digits[digest[index] & 0x0f];
      }
      const auto json = std::string{"{\"payload\":"} + payload +
                        ",\"auth\":{\"timestamp\":" + std::to_string(timestamp) + ",\"nonce\":\"" +
                        nonce + "\",\"signature\":\"" + signature + "\"}}";
      expect(::sendto(collector, json.data(), json.size(), 0, reinterpret_cast<sockaddr*>(&runtime),
                      runtime_size) > 0,
             "UDP runtime command delivery");
    };
    const auto await = [&](bool paused) {
      for (int attempt = 0; attempt < 50; ++attempt) {
        if (sink.paused() == paused) return true;
        std::this_thread::sleep_for(10ms);
      }
      return false;
    };
    command("pause", "transform");
    std::this_thread::sleep_for(20ms);
    expect(!sink.paused(), "UDP command for another node is ignored");
    command("pause", "generator", -1);
    std::this_thread::sleep_for(20ms);
    expect(!sink.paused(), "expired UDP command is ignored");
    command("pause");
    expect(await(true), "UDP pause command");
    command("resume");
    expect(await(false), "UDP resume command");
  }
  ::close(collector);
}

struct RawListener {
  int socket{-1};
  std::uint16_t port{};

  RawListener() {
    socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) throw std::runtime_error("raw listener socket");
    int reuse = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket, 4) != 0)
      throw std::runtime_error("raw listener bind");
    socklen_t size = sizeof(address);
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0)
      throw std::runtime_error("raw listener name");
    port = ntohs(address.sin_port);
  }
  RawListener(const RawListener&) = delete;
  ~RawListener() {
    if (socket >= 0) ::close(socket);
  }
};

std::uint16_t reserve_udp_port() {
  const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket < 0) throw std::runtime_error("UDP port reservation socket");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    ::close(socket);
    throw std::runtime_error("UDP port reservation bind");
  }
  socklen_t size = sizeof(address);
  if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
    ::close(socket);
    throw std::runtime_error("UDP port reservation name");
  }
  const auto port = ntohs(address.sin_port);
  ::close(socket);
  return port;
}

void send_raw_udp(std::uint16_t port, std::span<const std::byte> bytes) {
  const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket < 0) throw std::runtime_error("raw UDP socket");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  const auto sent = ::sendto(socket, bytes.data(), bytes.size(), 0,
                             reinterpret_cast<sockaddr*>(&address), sizeof(address));
  ::close(socket);
  expect(sent == static_cast<ssize_t>(bytes.size()), "raw UDP datagram send");
}

void udp_unicast_integrity_and_metrics() {
  const auto port = reserve_udp_port();
  graphx::UdpOptions options;
  options.receive_buffer_bytes = 65536;
  options.send_buffer_bytes = 65536;
  options.max_datagram_bytes = 256;
  graphx::MetricsTraceSink metrics;
  auto receiver =
      graphx::UdpTransport::listen({"127.0.0.1", port}, "127.0.0.1", "udp-test", &metrics, options);
  auto sender =
      graphx::UdpTransport::connect({"127.0.0.1", port}, "0.0.0.0", "udp-test", &metrics, options);

  expect(receiver.receive_result(2ms).status == graphx::ReceiveStatus::timeout,
         "UDP receive timeout is distinct");
  for (const auto sequence : {1ULL, 3ULL, 3ULL, 2ULL}) {
    sender.send(graphx::Envelope::make(sequence, "Datagram", std::to_string(sequence)));
    const auto result = receiver.receive_result(500ms);
    expect(result.status == graphx::ReceiveStatus::message && result.envelope->sequence == sequence,
           "UDP unicast delivery");
  }
  auto counters = metrics.edge("udp-test");
  expect(counters.sent == 4 && counters.received == 4 && counters.udp_sequence_gaps == 1 &&
             counters.udp_duplicates == 1 && counters.udp_out_of_order == 1,
         "UDP traffic and sequence anomaly metrics");

  const std::vector<std::byte> empty_datagram;
  send_raw_udp(port, empty_datagram);
  const std::array short_datagram{std::byte{0}, std::byte{1}};
  send_raw_udp(port, short_datagram);
  const auto canonical =
      graphx::frame(graphx::serialize(graphx::Envelope::make(9, "Datagram", "raw-validation")));
  auto length_mismatch = canonical;
  length_mismatch[3] =
      static_cast<std::byte>(std::to_integer<unsigned char>(length_mismatch[3]) - 1);
  send_raw_udp(port, length_mismatch);
  auto invalid_magic = canonical;
  invalid_magic[4] = std::byte{'B'};
  send_raw_udp(port, invalid_magic);
  auto unknown_version = canonical;
  unknown_version[7] = std::byte{99};
  send_raw_udp(port, unknown_version);
  auto trailing = canonical;
  trailing.push_back(std::byte{});
  const auto enlarged = static_cast<std::uint32_t>(trailing.size() - 4);
  trailing[0] = static_cast<std::byte>((enlarged >> 24) & 0xff);
  trailing[1] = static_cast<std::byte>((enlarged >> 16) & 0xff);
  trailing[2] = static_cast<std::byte>((enlarged >> 8) & 0xff);
  trailing[3] = static_cast<std::byte>(enlarged & 0xff);
  send_raw_udp(port, trailing);
  std::vector<std::byte> truncated(300, std::byte{0x55});
  send_raw_udp(port, truncated);
  sender.send(graphx::Envelope::make(4, "Datagram", "recovered"));
  const auto recovered = receiver.receive_result(500ms);
  expect(recovered.status == graphx::ReceiveStatus::message &&
             recovered.envelope->payload == "recovered",
         "UDP receiver recovers after malformed and truncated packets");
  counters = metrics.edge("udp-test");
  expect(counters.udp_malformed == 6 && counters.udp_truncated == 1,
         "UDP invalid datagram metrics");

  auto oversized = graphx::Envelope::make(5, "Datagram", std::string(300, 'x'));
  expect_failure([&] { sender.send(oversized); }, "max_datagram_bytes");
  expect(metrics.edge("udp-test").udp_oversized == 1, "UDP oversized send metric");

  auto moved_sender = std::move(sender);
  // Intentionally exercise the public moved-from guard.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  expect_failure([&] { sender.send(graphx::Envelope::make(6, "Datagram", "moved")); },
                 "moved-from");
  moved_sender.close();
  moved_sender.close();

  auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
  receiver.close();
  receiver.close();
  expect(blocked.wait_for(1s) == std::future_status::ready &&
             blocked.get().status == graphx::ReceiveStatus::cancelled,
         "UDP close promptly cancels blocked receive");
}

void udp_malformed_flood_preserves_deadline() {
  constexpr int flood_threads = 8;
  const auto port = reserve_udp_port();
  graphx::UdpOptions options;
  options.receive_buffer_bytes = 65536;
  options.send_buffer_bytes = 65536;
  options.max_datagram_bytes = 256;
  graphx::MetricsTraceSink metrics;
  auto receiver = graphx::UdpTransport::listen({"127.0.0.1", port}, "127.0.0.1", "udp-flood",
                                               &metrics, options);

  std::atomic<int> started{};
  std::atomic<bool> stop_requested{};
  std::atomic<bool> sender_failed{};
  std::vector<std::thread> senders;
  for (int index = 0; index < flood_threads; ++index) {
    senders.emplace_back([&] {
      const int descriptor = ::socket(AF_INET, SOCK_DGRAM, 0);
      if (descriptor < 0) {
        sender_failed.store(true);
        started.fetch_add(1);
        return;
      }
      sockaddr_in target{};
      target.sin_family = AF_INET;
      target.sin_port = htons(port);
      target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      const std::byte malformed{0x42};
      (void)::sendto(descriptor, &malformed, 1, 0, reinterpret_cast<sockaddr*>(&target),
                     sizeof(target));
      started.fetch_add(1);
      const auto stop = std::chrono::steady_clock::now() + 500ms;
      while (!stop_requested.load() && std::chrono::steady_clock::now() < stop)
        (void)::sendto(descriptor, &malformed, 1, 0, reinterpret_cast<sockaddr*>(&target),
                       sizeof(target));
      ::close(descriptor);
    });
  }
  while (started.load() != flood_threads) std::this_thread::yield();

  const auto begin = std::chrono::steady_clock::now();
  const auto result = receiver.receive_result(50ms);
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  stop_requested.store(true);
  for (auto& sender : senders) sender.join();

  expect(!sender_failed.load(), "create malformed UDP flood sockets");
  expect(result.status == graphx::ReceiveStatus::timeout,
         "malformed UDP flood preserves timeout outcome");
  expect(elapsed < 300ms, "malformed UDP flood preserves original receive deadline");
  expect(metrics.edge("udp-flood").udp_malformed > 0,
         "malformed UDP flood increments bounded counter");

  auto sender =
      graphx::UdpTransport::connect({"127.0.0.1", port}, "0.0.0.0", "udp-flood", nullptr, options);
  graphx::ReceiveResult recovered;
  for (int attempt = 0; attempt < 10 && !recovered.has_message(); ++attempt) {
    sender.send(graphx::Envelope::make(static_cast<std::uint64_t>(attempt + 1), "Recovery",
                                       "valid after flood"));
    recovered = receiver.receive_result(100ms);
  }
  expect(recovered.has_message() && recovered.envelope->payload == "valid after flood",
         "UDP receiver remains usable after malformed flood");
}

void udp_configured_datagram_boundaries() {
  const auto port = reserve_udp_port();
  auto boundary = graphx::Envelope::make(1, "Boundary", "");
  const auto empty_frame_size = graphx::frame(graphx::serialize(boundary)).size();
  constexpr std::size_t maximum = 256;
  expect(empty_frame_size < maximum, "UDP boundary fixture has payload capacity");
  auto maximum_boundary = boundary;
  maximum_boundary.payload.resize(maximum - empty_frame_size, 'x');
  expect(graphx::frame(graphx::serialize(maximum_boundary)).size() == maximum,
         "UDP fixture reaches exact configured datagram maximum");

  graphx::UdpOptions options;
  options.receive_buffer_bytes = 65536;
  options.send_buffer_bytes = 65536;
  options.max_datagram_bytes = maximum;
  auto receiver = graphx::UdpTransport::listen({"127.0.0.1", port}, "127.0.0.1", "udp-boundary",
                                               nullptr, options);
  auto sender = graphx::UdpTransport::connect({"127.0.0.1", port}, "0.0.0.0", "udp-boundary",
                                              nullptr, options);
  sender.send(boundary);
  const auto empty = receiver.receive_result(500ms);
  expect(empty.has_message() && empty.envelope->payload.empty(),
         "UDP zero-length envelope payload is accepted");

  boundary = std::move(maximum_boundary);
  boundary.sequence = 2;
  sender.send(boundary);
  const auto received = receiver.receive_result(500ms);
  expect(received.has_message() && received.envelope->payload == boundary.payload,
         "UDP exact configured datagram maximum is accepted");

  boundary.payload.push_back('x');
  expect_failure([&] { sender.send(boundary); }, "max_datagram_bytes");

  auto substantially_oversized =
      graphx::Envelope::make(3, "Boundary", std::string(1024 * 1024, 'x'));
  expect(graphx::serialized_size(substantially_oversized) > maximum,
         "UDP oversize preflight computes size without serialization");
  expect_failure([&] { sender.send(substantially_oversized); }, "max_datagram_bytes");
}

void udp_socket_error_metrics() {
  graphx::UdpOptions options;
  options.receive_buffer_bytes = 65536;
  options.send_buffer_bytes = 65536;
  options.max_datagram_bytes = 1400;
  graphx::MetricsTraceSink metrics;
  expect_failure(
      [&] {
        [[maybe_unused]] auto unavailable = graphx::UdpTransport::listen(
            {"192.0.2.1", reserve_udp_port()}, "127.0.0.1", "udp-socket-error", &metrics, options);
      },
      "bind UDP receiver");
  const auto counters = metrics.edge("udp-socket-error");
  expect(counters.udp_socket_errors == 1 && counters.errors == 1,
         "UDP socket failures have distinct counter and error diagnostics");

  // A socket explicitly bound to loopback cannot route to the TEST-NET-1
  // destination on supported hosts. Exercise repeated failures on one live
  // transport so every occurrence is counted while text is rate-limited.
  graphx::MetricsTraceSink repeated_metrics;
  CountingErrorTraceSink diagnostic_count;
  graphx::CompositeTraceSink traces;
  traces.add(repeated_metrics);
  traces.add(diagnostic_count);
  auto failing_sender = graphx::UdpTransport::connect({"192.0.2.1", 9}, "127.0.0.1",
                                                      "udp-repeated-error", &traces, options);
  for (int attempt = 0; attempt < 3; ++attempt)
    expect_failure([&] { failing_sender.send(graphx::Envelope::make(1, "Failure", "no route")); },
                   "send failed");
  expect(repeated_metrics.edge("udp-repeated-error").udp_socket_errors == 3 &&
             diagnostic_count.errors == 1,
         "UDP socket errors count every failure and rate-limit repeated diagnostics");
}

void udp_concurrent_close_stress() {
  constexpr int iterations = 100;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    graphx::UdpOptions options;
    options.receive_buffer_bytes = 65536;
    options.send_buffer_bytes = 65536;
    options.max_datagram_bytes = 1400;
    auto receiver = graphx::UdpTransport::listen({"127.0.0.1", reserve_udp_port()}, "127.0.0.1",
                                                 "udp-concurrent-close", nullptr, options);
    auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
    expect(blocked.wait_for(2ms) == std::future_status::timeout,
           "UDP receive entered its blocking path before concurrent close");
    auto first_close = std::async(std::launch::async, [&] { receiver.close(); });
    auto second_close = std::async(std::launch::async, [&] { receiver.close(); });
    expect(first_close.wait_for(1s) == std::future_status::ready &&
               second_close.wait_for(1s) == std::future_status::ready,
           "concurrent UDP close calls finish promptly");
    first_close.get();
    second_close.get();
    expect(blocked.wait_for(1s) == std::future_status::ready &&
               blocked.get().status == graphx::ReceiveStatus::cancelled,
           "concurrent UDP close cancels infinite receive");
  }
}

void udp_observer_failure_is_non_blocking() {
  const auto port = reserve_udp_port();
  graphx::UdpOptions options;
  options.receive_buffer_bytes = 65536;
  options.send_buffer_bytes = 65536;
  options.max_datagram_bytes = 1400;
  ThrowOnEveryTraceSink trace;
  auto receiver = graphx::UdpTransport::listen({"127.0.0.1", port}, "127.0.0.1", "udp-observer",
                                               &trace, options);
  auto sender = graphx::UdpTransport::connect({"127.0.0.1", port}, "0.0.0.0", "udp-observer",
                                              &trace, options);
  const std::array malformed{std::byte{0}, std::byte{1}};
  send_raw_udp(port, malformed);
  sender.send(graphx::Envelope::make(1, "Observer", "still delivered"));
  const auto received = receiver.receive_result(500ms);
  expect(received.has_message() && received.envelope->payload == "still delivered",
         "UDP observer failure cannot block processing");
  receiver.close();
  sender.close();
}

void udp_multicast_two_listeners() {
  const auto port = reserve_udp_port();
  constexpr std::string_view group = "239.255.42.1";
  graphx::UdpOptions options;
  options.mode = graphx::UdpMode::multicast;
  options.interface = "127.0.0.1";
  options.loopback = true;
  options.reuse_address = true;
  options.receive_buffer_bytes = 65536;
  options.send_buffer_bytes = 65536;
  options.max_datagram_bytes = 1400;
  auto first = graphx::UdpTransport::listen({"0.0.0.0", port}, std::string(group), "multicast-a",
                                            nullptr, options);
  auto second = graphx::UdpTransport::listen({"0.0.0.0", port}, std::string(group), "multicast-b",
                                             nullptr, options);
  auto sender = graphx::UdpTransport::connect({std::string(group), port}, "0.0.0.0",
                                              "multicast-publisher", nullptr, options);
  sender.send(graphx::Envelope::make(1, "Multicast", "one datagram"));
  const auto received_a = first.receive_result(1s);
  const auto received_b = second.receive_result(1s);
  expect(received_a.has_message() && received_b.has_message() &&
             received_a.envelope->payload == "one datagram" &&
             received_b.envelope->message_id == received_a.envelope->message_id,
         "one multicast datagram reaches two joined listeners");
  first.close();
  sender.send(graphx::Envelope::make(2, "Multicast", "remaining listener"));
  expect(second.receive_result(1s).has_message(),
         "closing one multicast listener does not disturb another");

  auto invalid_options = options;
  invalid_options.interface = "graphx-no-such-interface";
  expect_failure(
      [&] {
        [[maybe_unused]] auto invalid = graphx::UdpTransport::connect(
            {std::string(group), port}, "0.0.0.0", "invalid-interface", nullptr, invalid_options);
      },
      "has no configured IPv4 address");
}

std::string receive_http_request(int listener) {
  pollfd descriptor{listener, POLLIN, 0};
  expect(::poll(&descriptor, 1, 2000) > 0 && (descriptor.revents & POLLIN),
         "HTTP request accept deadline");
  const int client = ::accept(listener, nullptr, nullptr);
  if (client < 0) throw std::runtime_error("HTTP accept");
  std::string request;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::recv(client, buffer.data(), buffer.size(), 0);
    if (count <= 0) break;
    request.append(buffer.data(), static_cast<std::size_t>(count));
  }
  ::close(client);
  return request;
}

std::string http_json_value(const std::string& request, std::string_view name) {
  const auto marker = std::string{"\""} + std::string(name) + "\":\"";
  const auto begin = request.find(marker);
  if (begin == std::string::npos) return {};
  const auto value_begin = begin + marker.size();
  const auto end = request.find('"', value_begin);
  return request.substr(value_begin, end - value_begin);
}

bool valid_otlp_span_id(std::string_view value) {
  return value.size() == 16 && value.find_first_not_of('0') != std::string_view::npos &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
         });
}

void raw_write_all(int socket, std::span<const std::byte> bytes) {
  while (!bytes.empty()) {
    const auto sent = ::send(socket, bytes.data(), bytes.size(), 0);
    if (sent <= 0) throw std::runtime_error("raw send");
    bytes = bytes.subspan(static_cast<std::size_t>(sent));
  }
}

bool raw_read_all(int socket, std::span<std::byte> bytes) {
  while (!bytes.empty()) {
    const auto count = ::recv(socket, bytes.data(), bytes.size(), 0);
    if (count <= 0) return false;
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
  return true;
}

void raw_read_frame(int socket) {
  std::array<std::byte, 4> prefix{};
  expect(raw_read_all(socket, prefix), "raw frame header");
  std::vector<std::byte> payload(graphx::decode_frame_size(prefix));
  expect(raw_read_all(socket, payload), "raw frame payload");
}

std::vector<std::byte> framed_envelope(std::uint64_t sequence, std::string payload) {
  return graphx::frame(graphx::serialize(graphx::Envelope::make(sequence, "Raw", payload)));
}

void otlp_http_json_export() {
  RawListener listener;
  auto received = std::async(std::launch::async, [&] {
    std::array<std::string, 4> requests;
    for (auto& request : requests) request = receive_http_request(listener.socket);
    return requests;
  });
  auto envelope = graphx::Envelope::make(91, "Observed", "value");
  envelope.message_id = "00112233445566778899aabbccddeeff";
  envelope.trace_id = "102132435465768798a9babcbddcedfe";
  {
    graphx::OtlpHttpTraceSink producer("producer", "127.0.0.1", listener.port);
    producer.on_send("samples", envelope, 123);
  }
  {
    graphx::OtlpHttpTraceSink consumer("consumer", "127.0.0.1", listener.port);
    consumer.on_receive("samples", envelope, 123, std::chrono::microseconds(12));
  }
  {
    graphx::OtlpHttpTraceSink processor("processor", "127.0.0.1", listener.port);
    processor.on_processing("processor", envelope, std::chrono::microseconds(10), true);
    processor.on_processing("processor", envelope, std::chrono::microseconds(11), true);
  }
  const auto requests = received.get();
  std::unordered_set<std::string> span_ids;
  for (const auto& request : requests) {
    expect(request.find("POST /v1/traces HTTP/1.1") != std::string::npos, "OTLP HTTP endpoint");
    expect(request.find("\"resourceSpans\"") != std::string::npos &&
               request.find("\"graphx.sequence\"") != std::string::npos &&
               http_json_value(request, "traceId") == envelope.trace_id &&
               request.find("\"graphx.message_id\"") != std::string::npos &&
               request.find(envelope.message_id) != std::string::npos,
           "OTLP JSON span payload");
    const auto span_id = http_json_value(request, "spanId");
    expect(valid_otlp_span_id(span_id), "OTLP span identity is valid and non-zero");
    span_ids.insert(span_id);
  }
  expect(requests[0].find("graphx.send samples") != std::string::npos &&
             requests[1].find("graphx.receive samples") != std::string::npos,
         "OTLP producer and consumer spans");
  expect(span_ids.size() == requests.size(),
         "OTLP spans have distinct identities across exporters and repeated callbacks");
}

void otlp_span_ids_are_fork_safe() {
  RawListener listener;
  graphx::Envelope envelope{.sequence = 92,
                            .timestamp_ns = 1,
                            .type = "Forked",
                            .trace_id = "102132435465768798a9babcbddcedfe",
                            .attributes = {},
                            .payload = "value",
                            .message_id = "00112233445566778899aabbccddeeff",
                            .parent_message_id = {},
                            .wire_version = graphx::kEnvelopeWireVersion2};

  // Reproduce a prefork runtime that has already initialized the message-ID
  // generator. Span identity must not depend on that inherited process state.
  static_cast<void>(graphx::generate_identity());
  const auto child = ::fork();
  expect(child >= 0, "OTLP fork");
  if (child == 0) {
    ::close(listener.socket);
    listener.socket = -1;
    try {
      {
        graphx::OtlpHttpTraceSink exporter("child", "127.0.0.1", listener.port);
        exporter.on_send("samples", envelope, 123);
      }
      ::_exit(0);
    } catch (...) {
      ::_exit(1);
    }
  }

  {
    graphx::OtlpHttpTraceSink exporter("parent", "127.0.0.1", listener.port);
    exporter.on_receive("samples", envelope, 123, std::chrono::microseconds(12));
  }
  const std::array requests{receive_http_request(listener.socket),
                            receive_http_request(listener.socket)};
  int child_status{};
  expect(::waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) &&
             WEXITSTATUS(child_status) == 0,
         "forked OTLP exporter exit");
  const auto first_span = http_json_value(requests[0], "spanId");
  const auto second_span = http_json_value(requests[1], "spanId");
  expect(http_json_value(requests[0], "traceId") == envelope.trace_id &&
             http_json_value(requests[1], "traceId") == envelope.trace_id,
         "forked OTLP exporters preserve canonical trace identity");
  expect(valid_otlp_span_id(first_span) && valid_otlp_span_id(second_span) &&
             first_span != second_span,
         "forked OTLP exporters generate distinct valid span identities");
}

std::string shared_segment(std::string_view suffix) {
  static unsigned counter{};
  return "/gx-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) + "-" +
         std::string(suffix);
}

void shared_memory_wraparound_and_cleanup() {
  const auto segment = shared_segment("wrap");
  graphx::SharedMemoryOptions options;
  options.capacity = 3;
  options.max_message_bytes = 4096;
  graphx::MetricsTraceSink metrics;
  auto consumer = graphx::SharedMemoryTransport::listen(segment, "shm-wrap", &metrics, options);
  auto producer = graphx::SharedMemoryTransport::connect(segment, "shm-wrap", &metrics, options);
  try {
    [[maybe_unused]] auto duplicate =
        graphx::SharedMemoryTransport::connect(segment, "shm-duplicate-producer", nullptr, options);
    throw std::runtime_error("second shared-memory producer was accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("live producer") != std::string_view::npos,
           "shared-memory producer ownership");
  }
  for (std::uint64_t sequence = 1; sequence <= 20; ++sequence) {
    producer.send(graphx::Envelope::make(sequence, "Shared", std::to_string(sequence)));
    const auto received = consumer.receive(100ms);
    expect(received && received->sequence == sequence, "shared-memory ring wraparound");
  }
  producer.close();
  expect(!consumer.receive(100ms), "shared-memory close after drain");
  const auto measured = metrics.edge("shm-wrap");
  expect(measured.sent == 20 && measured.received == 20 && measured.sent_wire_bytes > 0 &&
             measured.received_wire_bytes > 0,
         "shared-memory tracing hooks");
  consumer.close();
  const int stale = ::shm_open(segment.c_str(), O_RDWR, 0600);
  expect(stale < 0 && errno == ENOENT, "shared-memory owner unlinks segment");
  if (stale >= 0) ::close(stale);
}

void shared_memory_backpressure_and_limits() {
  graphx::SharedMemoryOptions options;
  options.capacity = 1;
  options.max_message_bytes = 256;
  options.backpressure = graphx::SharedMemoryBackpressure::reject;
  const auto reject_segment = shared_segment("reject");
  auto consumer =
      graphx::SharedMemoryTransport::listen(reject_segment, "shm-reject", nullptr, options);
  auto producer =
      graphx::SharedMemoryTransport::connect(reject_segment, "shm-reject", nullptr, options);
  producer.send(graphx::Envelope::make(1, "Shared", "first"));
  try {
    producer.send(graphx::Envelope::make(2, "Shared", "second"));
    throw std::runtime_error("full reject ring accepted a message");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("backpressure=reject") != std::string_view::npos,
           "shared-memory reject policy");
  }
  const auto queued = consumer.receive(100ms);
  expect(queued && queued->sequence == 1, "shared-memory rejected no queued data");
  try {
    producer.send(graphx::Envelope::make(3, "Shared", std::string(512, 'x')));
    throw std::runtime_error("oversized shared-memory frame accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("maximum message size") != std::string_view::npos,
           "shared-memory maximum message size");
  }
  producer.close();
  consumer.close();

  options.backpressure = graphx::SharedMemoryBackpressure::block;
  options.send_timeout = 25ms;
  options.max_message_bytes = 4096;
  const auto block_segment = shared_segment("block");
  auto blocked_consumer =
      graphx::SharedMemoryTransport::listen(block_segment, "shm-block", nullptr, options);
  auto blocked_producer =
      graphx::SharedMemoryTransport::connect(block_segment, "shm-block", nullptr, options);
  blocked_producer.send(graphx::Envelope::make(4, "Shared", "first"));
  try {
    blocked_producer.send(graphx::Envelope::make(5, "Shared", "blocked"));
    throw std::runtime_error("blocked shared-memory send ignored deadline");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("timed out") != std::string_view::npos,
           "shared-memory blocking deadline");
  }
  blocked_producer.close();
  blocked_consumer.close();
}

void shared_memory_receive_timeout_and_settings() {
  const auto segment = shared_segment("timeout");
  graphx::SharedMemoryOptions options;
  options.capacity = 2;
  options.max_message_bytes = 1024;
  auto consumer = graphx::SharedMemoryTransport::listen(segment, "shm-timeout", nullptr, options);
  expect(!consumer.receive(20ms), "empty shared-memory receive timeout");
  try {
    [[maybe_unused]] auto duplicate =
        graphx::SharedMemoryTransport::listen(segment, "shm-duplicate", nullptr, options);
    throw std::runtime_error("live shared-memory consumer was replaced");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("live consumer") != std::string_view::npos,
           "shared-memory live owner protection");
  }
  auto mismatch = options;
  mismatch.capacity = 3;
  try {
    [[maybe_unused]] auto ignored =
        graphx::SharedMemoryTransport::connect(segment, "shm-mismatch", nullptr, mismatch);
    throw std::runtime_error("mismatched shared-memory layout accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("do not match") != std::string_view::npos,
           "shared-memory layout validation");
  }
  consumer.close();
}

void shared_memory_cross_process_and_crash_detection() {
  const auto segment = shared_segment("process");
  graphx::SharedMemoryOptions options;
  options.capacity = 4;
  options.max_message_bytes = 4096;
  auto consumer = graphx::SharedMemoryTransport::listen(segment, "shm-process", nullptr, options);
  const pid_t child = ::fork();
  if (child < 0) throw std::runtime_error("fork shared-memory producer");
  if (child == 0) {
    try {
      auto producer =
          graphx::SharedMemoryTransport::connect(segment, "shm-process", nullptr, options);
      producer.send(graphx::Envelope::make(77, "Shared", "from child"));
      ::_exit(0);  // Deliberately skip cleanup to exercise peer-death detection.
    } catch (...) {
      ::_exit(2);
    }
  }
  const auto received = consumer.receive(2s);
  expect(received && received->sequence == 77 && received->payload == "from child",
         "cross-process shared-memory delivery");
  int status{};
  expect(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
         "shared-memory child status");
  expect(!consumer.receive(2s), "shared-memory producer crash detection");
  consumer.close();

  const auto stale_segment = shared_segment("stale");
  const pid_t stale_child = ::fork();
  if (stale_child < 0) throw std::runtime_error("fork stale shared-memory listener");
  if (stale_child == 0) {
    try {
      [[maybe_unused]] auto stale =
          graphx::SharedMemoryTransport::listen(stale_segment, "shm-stale", nullptr, options);
      ::_exit(0);
    } catch (...) {
      ::_exit(2);
    }
  }
  expect(::waitpid(stale_child, &status, 0) == stale_child && WIFEXITED(status) &&
             WEXITSTATUS(status) == 0,
         "stale shared-memory listener status");
  auto replacement =
      graphx::SharedMemoryTransport::listen(stale_segment, "shm-replacement", nullptr, options);
  replacement.close();
}

void tcp_fragmented_and_consecutive_frames() {
  RawListener listener;
  std::promise<void> first_fragment_sent;
  std::promise<void> continue_send;
  auto release = continue_send.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    const auto first = framed_envelope(11, "fragmented");
    const auto second = framed_envelope(12, "consecutive");
    raw_write_all(peer, std::span(first).first(2));
    first_fragment_sent.set_value();
    release.wait();
    raw_write_all(peer, std::span(first).subspan(2, 5));
    raw_write_all(peer, std::span(first).subspan(7));
    raw_write_all(peer, second);
    ::shutdown(peer, SHUT_RDWR);
    ::close(peer);
  });
  auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "fragmented");
  first_fragment_sent.get_future().wait();
  auto pending = std::async(std::launch::async, [&] { return client.receive(2s); });
  expect(pending.wait_for(20ms) == std::future_status::timeout,
         "partial header must not produce a frame");
  continue_send.set_value();
  const auto first = pending.get();
  const auto second = client.receive(2s);
  expect(first && first->sequence == 11 && first->payload == "fragmented",
         "fragmented frame delivery");
  expect(second && second->sequence == 12 && second->payload == "consecutive",
         "consecutive frame delivery");
  expect(!client.receive(2s), "peer closure between frames");
  server.join();
}

void tcp_truncated_and_oversized_frames() {
  auto run = [](std::vector<std::byte> bytes, std::string_view expected) {
    RawListener listener;
    std::thread server([&] {
      const int peer = ::accept(listener.socket, nullptr, nullptr);
      raw_write_all(peer, bytes);
      ::shutdown(peer, SHUT_RDWR);
      ::close(peer);
    });
    auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "bad-frame");
    try {
      [[maybe_unused]] auto ignored = client.receive(2s);
      throw std::runtime_error("invalid frame was accepted");
    } catch (const std::exception& error) {
      expect(std::string_view(error.what()).find(expected) != std::string_view::npos,
             "contextual frame error");
      expect(std::string_view(error.what()).find("bad-frame") != std::string_view::npos,
             "edge id in frame error");
    }
    server.join();
  };

  run({std::byte{0}, std::byte{0}}, "header");
  run({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{8}, std::byte{1}}, "payload");
  run({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}}, "malformed envelope");
  const auto too_large = graphx::kMaxFrameBytes + 1;
  run({static_cast<std::byte>((too_large >> 24) & 0xff),
       static_cast<std::byte>((too_large >> 16) & 0xff),
       static_cast<std::byte>((too_large >> 8) & 0xff), static_cast<std::byte>(too_large & 0xff)},
      "exceeds configured maximum");
}

void tcp_receive_timeout_covers_partial_frame() {
  RawListener listener;
  std::promise<void> release_server;
  auto release = release_server.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    raw_write_all(peer, {reinterpret_cast<const std::byte*>("\0\0"), 2});
    release.wait();
    ::close(peer);
  });
  auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "deadline");
  try {
    [[maybe_unused]] auto ignored = client.receive(30ms);
    throw std::runtime_error("partial frame timeout was accepted");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("timed out during frame header") !=
               std::string_view::npos,
           "full-frame receive deadline");
  }
  release_server.set_value();
  server.join();
}

void tcp_receive_timeout_without_data() {
  RawListener listener;
  std::promise<void> peer_ready;
  std::promise<void> release_server;
  auto release = release_server.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    peer_ready.set_value();
    release.wait();
    ::close(peer);
  });
  auto client = graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "idle-timeout");
  peer_ready.get_future().wait();
  expect(!client.receive(20ms), "idle receive timeout returns no envelope");
  release_server.set_value();
  server.join();
}

void tcp_send_backpressure_has_deadline() {
  RawListener listener;
  std::promise<void> receiver_ready;
  std::promise<void> release_server;
  auto release = release_server.get_future();
  std::thread server([&] {
    const int peer = ::accept(listener.socket, nullptr, nullptr);
    int receive_buffer = 1024;
    ::setsockopt(peer, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
    receiver_ready.set_value();
    release.wait();
    ::close(peer);
  });
  graphx::TcpOptions options;
  options.send_timeout = 30ms;
  auto client =
      graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "backpressure", nullptr, options);
  receiver_ready.get_future().wait();
  try {
    client.send(graphx::Envelope::make(19, "Large", std::string(15 * 1024 * 1024, 'x')));
    throw std::runtime_error("blocked send did not respect its deadline");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("timed out") != std::string_view::npos,
           "bounded blocking backpressure");
  }
  release_server.set_value();
  server.join();
}

void tcp_reconnects_without_sigpipe() {
  RawListener listener;
  std::promise<void> first_closed;
  std::thread server([&] {
    int peer = ::accept(listener.socket, nullptr, nullptr);
    raw_read_frame(peer);
    ::shutdown(peer, SHUT_RDWR);
    ::close(peer);
    first_closed.set_value();
    peer = ::accept(listener.socket, nullptr, nullptr);
    raw_read_frame(peer);
    ::close(peer);
  });
  graphx::TcpOptions options;
  options.reconnect = true;
  options.retry = {20, 5ms, 20ms};
  auto client =
      graphx::TcpTransport::connect({"127.0.0.1", listener.port}, "reconnect", nullptr, options);
  client.send(graphx::Envelope::make(20, "Reconnect", "first"));
  first_closed.get_future().wait();
  expect(!client.receive(2s), "closure marks outbound connection disconnected");
  client.send(graphx::Envelope::make(21, "Reconnect", "second"));
  server.join();
}

void tcp_listener_reaccepts_and_close_cancels() {
  std::uint16_t port;
  {
    RawListener reservation;
    port = reservation.port;
  }
  graphx::TcpOptions options;
  options.reconnect = true;
  auto listener = graphx::TcpTransport::listen({"127.0.0.1", port}, "listener", nullptr, options);
  auto received = std::async(std::launch::async, [&] {
    const auto first = listener.receive(2s);
    const auto second = listener.receive(2s);
    return first && second && first->sequence == 31 && second->sequence == 32;
  });
  {
    auto client = graphx::TcpTransport::connect({"127.0.0.1", port}, "first-client");
    client.send(graphx::Envelope::make(31, "Reconnect", "first"));
  }
  {
    auto client = graphx::TcpTransport::connect({"127.0.0.1", port}, "second-client");
    client.send(graphx::Envelope::make(32, "Reconnect", "second"));
  }
  expect(received.get(), "listener accepts a replacement connection");

  auto cancelled = std::async(std::launch::async, [&] {
    return listener.receive_result().status == graphx::ReceiveStatus::cancelled;
  });
  listener.close();
  expect(cancelled.wait_for(1s) == std::future_status::ready && cancelled.get(),
         "close cancels a blocked listener receive");
}

void tcp_peer_close_respects_reconnect_policy() {
  const auto run = [](bool reconnect) {
    std::uint16_t port;
    {
      RawListener reservation;
      port = reservation.port;
    }
    graphx::TcpOptions options;
    options.reconnect = reconnect;
    auto listener =
        graphx::TcpTransport::listen({"127.0.0.1", port}, "reconnect-policy", nullptr, options);
    { auto client = graphx::TcpTransport::connect({"127.0.0.1", port}, "reconnect-policy-client"); }
    return listener.receive_result(50ms).status;
  };

  expect(run(false) == graphx::ReceiveStatus::end_of_stream,
         "TCP peer close is terminal when reconnect is disabled");
  expect(run(true) == graphx::ReceiveStatus::timeout,
         "TCP peer close is transient when reconnect is enabled");
}

void tcp_end_to_end() {
  const auto port = static_cast<std::uint16_t>(42000 + (::getpid() % 1000));
  std::exception_ptr server_error;
  std::thread server([&] {
    try {
      auto receiver = graphx::TcpTransport::listen({"127.0.0.1", port}, "test");
      auto envelope = receiver.receive(std::chrono::seconds(2));
      expect(envelope && envelope->payload == "over tcp", "tcp delivery");
      envelope->payload = "ack";
      receiver.send(*envelope);
    } catch (...) {
      server_error = std::current_exception();
    }
  });

  try {
    std::optional<graphx::TcpTransport> sender;
    std::string connect_error;
    for (int attempt = 0; attempt < 20 && !sender; ++attempt) {
      try {
        sender.emplace(graphx::TcpTransport::connect({"127.0.0.1", port}, "test"));
      } catch (const std::exception& error) {
        connect_error = error.what();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
    if (!sender) throw std::runtime_error("tcp server did not become ready: " + connect_error);
    sender->send(graphx::Envelope::make(9, "Ping", "over tcp"));
    auto reply = sender->receive(std::chrono::seconds(2));
    expect(reply && reply->payload == "ack" && reply->sequence == 9, "tcp reply");
  } catch (...) {
    if (server.joinable()) server.join();
    throw;
  }
  server.join();
  if (server_error) std::rethrow_exception(server_error);
}

void tcp_mixed_version_interoperability() {
  std::uint16_t port;
  {
    RawListener reservation;
    port = reservation.port;
  }
  auto legacy = graphx::deserialize(golden_bytes("envelope-v1.hex"));
  auto current = graphx::deserialize(golden_bytes("envelope-v2.hex"));
  auto receiver = graphx::TcpTransport::listen({"127.0.0.1", port}, "mixed-version");
  auto received = std::async(std::launch::async, [&] {
    return std::array{receiver.receive_result(2s), receiver.receive_result(2s)};
  });
  auto sender = graphx::TcpTransport::connect({"127.0.0.1", port}, "mixed-version");
  sender.send(legacy);
  sender.send(current);
  const auto messages = received.get();
  expect(messages[0].status == graphx::ReceiveStatus::message &&
             messages[0].envelope->wire_version == graphx::kEnvelopeWireVersion1 &&
             graphx::serialize(*messages[0].envelope) == golden_bytes("envelope-v1.hex"),
         "TCP reader preserves exact version-1 envelope");
  expect(messages[1].status == graphx::ReceiveStatus::message &&
             messages[1].envelope->wire_version == graphx::kEnvelopeWireVersion2 &&
             messages[1].envelope->message_id == current.message_id &&
             messages[1].envelope->trace_id == current.trace_id,
         "TCP reader accepts version 2 after version 1 on one connection");
  sender.close();
  receiver.close();
}

void unix_socket_end_to_end() {
  const auto path = "/tmp/graphx-test-" + std::to_string(::getpid()) + ".sock";
  std::exception_ptr server_error;
  graphx::MetricsTraceSink metrics;
  std::thread server([&] {
    try {
      auto receiver = graphx::UnixDomainSocketTransport::listen(path, "test-uds", &metrics);
      auto envelope = receiver.receive(std::chrono::seconds(2));
      expect(envelope && envelope->payload == "over uds", "Unix socket delivery");
      envelope->payload = "ack";
      receiver.send(*envelope);
    } catch (...) {
      server_error = std::current_exception();
    }
  });
  try {
    std::optional<graphx::UnixDomainSocketTransport> sender;
    std::string connect_error;
    for (int attempt = 0; attempt < 20 && !sender; ++attempt) {
      try {
        sender.emplace(graphx::UnixDomainSocketTransport::connect(path, "test-uds", &metrics));
      } catch (const std::exception& error) {
        connect_error = error.what();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
    if (!sender) throw std::runtime_error("Unix socket did not become ready: " + connect_error);
    sender->send(graphx::Envelope::make(10, "Ping", "over uds"));
    auto reply = sender->receive(std::chrono::seconds(2));
    expect(reply && reply->payload == "ack", "Unix socket reply");
  } catch (...) {
    if (server.joinable()) server.join();
    throw;
  }
  server.join();
  if (server_error) std::rethrow_exception(server_error);
  const auto measured = metrics.edge("test-uds");
  expect(measured.sent == 2 && measured.received == 2, "Unix socket tracing parity");
}

void unix_socket_listener_is_interruptible_before_accept() {
  const auto path = "/tmp/graphx-cancel-" + std::to_string(::getpid()) + ".sock";
  graphx::UnixDomainSocketOptions invalid;
  invalid.send_timeout = 0ms;
  try {
    [[maybe_unused]] auto rejected =
        graphx::UnixDomainSocketTransport::listen(path, "invalid-uds", nullptr, invalid);
    throw std::runtime_error("invalid Unix-domain timeout was accepted");
  } catch (const std::invalid_argument&) {
  }
  auto listener = graphx::UnixDomainSocketTransport::listen(path, "cancel-uds");
  expect(listener.receive_result(5ms).status == graphx::ReceiveStatus::timeout,
         "Unix listener startup receive has a deadline");
  auto cancelled = std::async(std::launch::async, [&] { return listener.receive_result(); });
  listener.close();
  expect(cancelled.wait_for(1s) == std::future_status::ready &&
             cancelled.get().status == graphx::ReceiveStatus::cancelled,
         "Unix listener close cancels receive before first accept");
  expect(!std::filesystem::exists(path), "Unix listener removes owned socket path");
}

void unix_socket_invalidates_failed_frames() {
  static unsigned counter{};
  const auto run = [&](std::string_view suffix, std::span<const std::byte> bytes, bool close_peer,
                       std::string_view expected) {
    const auto path = "/tmp/graphx-invalid-" + std::to_string(::getpid()) + "-" +
                      std::to_string(counter++) + "-" + std::string(suffix) + ".sock";
    auto receiver = graphx::UnixDomainSocketTransport::listen(path, "invalid-uds");
    const int peer = ::socket(AF_UNIX, SOCK_STREAM, 0);
    expect(peer >= 0, "create raw Unix peer");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    expect(::connect(peer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
           "connect raw Unix peer");
    raw_write_all(peer, bytes);
    if (close_peer) ::shutdown(peer, SHUT_RDWR);
    bool rejected{};
    try {
      [[maybe_unused]] const auto result = receiver.receive_result(20ms);
    } catch (const std::exception& error) {
      rejected = std::string_view(error.what()).find(expected) != std::string_view::npos;
    }
    expect(rejected, "Unix invalid frame has contextual failure");
    expect(receiver.receive_result(5ms).status == graphx::ReceiveStatus::end_of_stream,
           "Unix framing failure leaves transport terminal");
    ::close(peer);
    receiver.close();
    expect(!std::filesystem::exists(path), "Unix invalid-frame listener removes socket path");
  };

  const std::array partial_header{std::byte{0x00}, std::byte{0x00}};
  run("header-timeout", partial_header, false, "frame header");

  const auto framed = framed_envelope(41, "partial payload");
  run("payload-close", std::span(framed).first(7), true, "frame payload");

  const std::array oversized_prefix{std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                                    std::byte{0x01}};
  run("oversized", oversized_prefix, false, "maximum");

  const std::array malformed{std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
                             std::byte{0xff}};
  run("malformed", malformed, false, "envelope");
}

void observer_failure_does_not_break_close() {
  ThrowOnCloseTraceSink trace;

  auto channel = std::make_shared<graphx::InProcessChannel>();
  graphx::InProcessTransport in_process(channel, "observer-in-process", &trace);
  auto in_process_receive =
      std::async(std::launch::async, [&] { return in_process.receive_result(); });
  expect(in_process_receive.wait_for(20ms) == std::future_status::timeout,
         "in-process receive is blocked before close");
  in_process.close();
  expect(in_process_receive.wait_for(1s) == std::future_status::ready &&
             in_process_receive.get().status == graphx::ReceiveStatus::cancelled,
         "in-process close ignores observer failure and cancels receive");

  const auto segment = shared_segment("observer");
  auto shared = graphx::SharedMemoryTransport::listen(segment, "observer-shared", &trace);
  auto shared_receive = std::async(std::launch::async, [&] { return shared.receive_result(); });
  expect(shared_receive.wait_for(20ms) == std::future_status::timeout,
         "shared-memory receive is blocked before close");
  shared.close();
  expect(shared_receive.wait_for(1s) == std::future_status::ready &&
             shared_receive.get().status == graphx::ReceiveStatus::cancelled,
         "shared-memory close ignores observer failure and cancels receive");

  std::uint16_t port;
  {
    RawListener reservation;
    port = reservation.port;
  }
  auto tcp = graphx::TcpTransport::listen({"127.0.0.1", port}, "observer-tcp", &trace);
  auto tcp_receive = std::async(std::launch::async, [&] { return tcp.receive_result(); });
  expect(tcp_receive.wait_for(20ms) == std::future_status::timeout,
         "TCP receive is blocked before close");
  tcp.close();
  expect(tcp_receive.wait_for(1s) == std::future_status::ready &&
             tcp_receive.get().status == graphx::ReceiveStatus::cancelled,
         "TCP close ignores observer failure and cancels receive");

  graphx::UdpOptions udp_options;
  udp_options.receive_buffer_bytes = 65536;
  udp_options.send_buffer_bytes = 65536;
  const auto udp_port = reserve_udp_port();
  auto udp = graphx::UdpTransport::listen({"127.0.0.1", udp_port}, "127.0.0.1", "observer-udp",
                                          &trace, udp_options);
  auto udp_receive = std::async(std::launch::async, [&] { return udp.receive_result(); });
  expect(udp_receive.wait_for(20ms) == std::future_status::timeout,
         "UDP receive is blocked before close");
  udp.close();
  expect(udp_receive.wait_for(1s) == std::future_status::ready &&
             udp_receive.get().status == graphx::ReceiveStatus::cancelled,
         "UDP close ignores observer failure and cancels receive");

  const auto path = "/tmp/graphx-observer-" + std::to_string(::getpid()) + ".sock";
  auto unix = graphx::UnixDomainSocketTransport::listen(path, "observer-unix", &trace);
  auto unix_receive = std::async(std::launch::async, [&] { return unix.receive_result(); });
  expect(unix_receive.wait_for(20ms) == std::future_status::timeout,
         "Unix receive is blocked before close");
  unix.close();
  expect(unix_receive.wait_for(1s) == std::future_status::ready &&
             unix_receive.get().status == graphx::ReceiveStatus::cancelled,
         "Unix close ignores observer failure and cancels receive");
}

void unix_socket_send_has_deadline() {
  const auto path = "/tmp/graphx-pressure-" + std::to_string(::getpid()) + ".sock";
  ::unlink(path.c_str());
  const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server < 0) throw std::runtime_error("create raw Unix listener");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  if (::bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(server, 1) != 0) {
    ::close(server);
    ::unlink(path.c_str());
    throw std::runtime_error("bind raw Unix listener");
  }
  std::promise<void> accepted;
  std::promise<void> release;
  auto released = release.get_future();
  std::thread peer([&] {
    const int socket = ::accept(server, nullptr, nullptr);
    int receive_buffer = 1024;
    ::setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
    accepted.set_value();
    released.wait();
    ::close(socket);
  });
  graphx::UnixDomainSocketOptions options;
  options.send_timeout = 30ms;
  auto client = graphx::UnixDomainSocketTransport::connect(path, "uds-pressure", nullptr, options);
  accepted.get_future().wait();
  bool timed_out{};
  try {
    client.send(graphx::Envelope::make(1, "Large", std::string(15 * 1024 * 1024, 'x')));
  } catch (const std::exception& error) {
    timed_out = std::string_view(error.what()).find("timed out") != std::string_view::npos;
  }
  release.set_value();
  peer.join();
  ::close(server);
  ::unlink(path.c_str());
  expect(timed_out, "Unix-domain blocked send respects configured deadline");
  expect(client.receive_result(5ms).status == graphx::ReceiveStatus::end_of_stream,
         "Unix-domain send failure leaves transport terminal");
  try {
    client.send(graphx::Envelope::make(2, "AfterFailure", "must fail"));
    throw std::runtime_error("Unix-domain send succeeded after terminal failure");
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find("no accepted peer") != std::string_view::npos,
           "Unix-domain second send reports terminal peer state");
  }
}

void transport_lifecycle_stress() {
  constexpr int iterations = 12;
  const auto segment = shared_segment("stress-reuse");
  const auto unix_path = "/tmp/graphx-stress-" + std::to_string(::getpid()) + ".sock";
  for (int iteration = 0; iteration < iterations; ++iteration) {
    {
      auto channel = std::make_shared<graphx::InProcessChannel>();
      graphx::InProcessTransport sender(channel, "stress-in-process"),
          receiver(channel, "stress-in-process");
      sender.send(graphx::Envelope::make(iteration + 1, "Stress", "in-process"));
      expect(receiver.receive_result(1s).status == graphx::ReceiveStatus::message,
             "repeated in-process delivery");
      auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
      receiver.close();
      expect(blocked.wait_for(1s) == std::future_status::ready &&
                 blocked.get().status == graphx::ReceiveStatus::cancelled,
             "repeated in-process close cancels receive");
      sender.close();
    }
    {
      const auto port = reserve_udp_port();
      graphx::UdpOptions options;
      options.receive_buffer_bytes = 65536;
      options.send_buffer_bytes = 65536;
      options.max_datagram_bytes = 1400;
      auto receiver = graphx::UdpTransport::listen({"127.0.0.1", port}, "127.0.0.1", "stress-udp",
                                                   nullptr, options);
      auto sender = graphx::UdpTransport::connect({"127.0.0.1", port}, "0.0.0.0", "stress-udp",
                                                  nullptr, options);
      sender.send(graphx::Envelope::make(iteration + 1, "Stress", "udp"));
      expect(receiver.receive_result(1s).status == graphx::ReceiveStatus::message,
             "repeated UDP delivery");
      auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
      receiver.close();
      expect(blocked.wait_for(1s) == std::future_status::ready &&
                 blocked.get().status == graphx::ReceiveStatus::cancelled,
             "repeated UDP close cancels receive");
      sender.close();
    }
    {
      auto receiver = graphx::SharedMemoryTransport::listen(segment, "stress-shared");
      auto sender = graphx::SharedMemoryTransport::connect(segment, "stress-shared");
      sender.send(graphx::Envelope::make(iteration + 1, "Stress", "shared-memory"));
      expect(receiver.receive_result(1s).status == graphx::ReceiveStatus::message,
             "repeated shared-memory delivery");
      auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
      receiver.close();
      expect(blocked.wait_for(1s) == std::future_status::ready &&
                 blocked.get().status == graphx::ReceiveStatus::cancelled,
             "repeated shared-memory close cancels receive");
      sender.close();
      const int stale = ::shm_open(segment.c_str(), O_RDWR, 0600);
      expect(stale < 0 && errno == ENOENT, "repeated shared-memory close unlinks segment");
      if (stale >= 0) ::close(stale);
    }
    {
      std::uint16_t port;
      {
        RawListener reservation;
        port = reservation.port;
      }
      graphx::TcpOptions options;
      options.reconnect = true;
      auto receiver =
          graphx::TcpTransport::listen({"127.0.0.1", port}, "stress-tcp", nullptr, options);
      for (int peer = 0; peer < 2; ++peer) {
        auto sender = graphx::TcpTransport::connect({"127.0.0.1", port}, "stress-tcp-client");
        sender.send(graphx::Envelope::make(iteration * 2 + peer + 1, "Stress", "tcp"));
        expect(receiver.receive_result(1s).status == graphx::ReceiveStatus::message,
               "repeated TCP connected delivery");
        sender.close();
        expect(receiver.receive_result(20ms).status == graphx::ReceiveStatus::timeout,
               "repeated TCP listener remains available after peer close");
      }
      auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
      receiver.close();
      expect(blocked.wait_for(1s) == std::future_status::ready &&
                 blocked.get().status == graphx::ReceiveStatus::cancelled,
             "repeated TCP close cancels receive");
    }
    {
      auto receiver =
          graphx::UnixDomainSocketTransport::listen(unix_path, "stress-unix-domain-socket");
      auto sender =
          graphx::UnixDomainSocketTransport::connect(unix_path, "stress-unix-domain-socket");
      sender.send(graphx::Envelope::make(iteration + 1, "Stress", "unix-domain-socket"));
      expect(receiver.receive_result(1s).status == graphx::ReceiveStatus::message,
             "repeated Unix-domain connected delivery");
      auto blocked = std::async(std::launch::async, [&] { return receiver.receive_result(); });
      receiver.close();
      expect(blocked.wait_for(1s) == std::future_status::ready &&
                 blocked.get().status == graphx::ReceiveStatus::cancelled,
             "repeated Unix-domain close cancels receive");
      sender.close();
      expect(!std::filesystem::exists(unix_path),
             "repeated Unix-domain close removes reusable socket path");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::pair<const char*, std::function<void()>> tests[] = {
      {"framing", framing},
      {"envelope", envelope_round_trip},
      {"envelope protocol golden vectors", envelope_protocol_golden_vectors},
      {"envelope identity semantics", envelope_identity_semantics},
      {"envelope protocol invalid input", envelope_protocol_rejects_invalid_input},
      {"protocol exhaustive boundaries", envelope_protocol_exhaustive_boundaries},
      {"legacy transport adapter", legacy_transport_adapter},
      {"PCAPNG capture", pcapng_capture},
      {"Ethernet PCAPNG capture", ethernet_pcapng_capture},
      {"in-process", in_process},
      {"in-process typed outcomes", in_process_typed_outcomes_and_backpressure},
      {"in-process validation atomicity", in_process_validation_is_atomic},
      {"metrics", metrics_sink},
      {"UDP unicast integrity and metrics", udp_unicast_integrity_and_metrics},
      {"UDP malformed flood deadline", udp_malformed_flood_preserves_deadline},
      {"UDP configured datagram boundaries", udp_configured_datagram_boundaries},
      {"UDP socket error metrics", udp_socket_error_metrics},
      {"UDP concurrent close stress", udp_concurrent_close_stress},
      {"UDP observer failure", udp_observer_failure_is_non_blocking},
      {"UDP multicast two listeners", udp_multicast_two_listeners},
      {"UDP runtime control", udp_runtime_control},
      {"OTLP HTTP JSON", otlp_http_json_export},
      {"OTLP fork-safe span identities", otlp_span_ids_are_fork_safe},
      {"shared-memory wraparound", shared_memory_wraparound_and_cleanup},
      {"shared-memory pressure", shared_memory_backpressure_and_limits},
      {"shared-memory timeout", shared_memory_receive_timeout_and_settings},
      {"shared-memory process", shared_memory_cross_process_and_crash_detection},
      {"TCP fragmented and consecutive", tcp_fragmented_and_consecutive_frames},
      {"TCP truncated and oversized", tcp_truncated_and_oversized_frames},
      {"TCP partial-frame timeout", tcp_receive_timeout_covers_partial_frame},
      {"TCP idle timeout", tcp_receive_timeout_without_data},
      {"TCP send backpressure", tcp_send_backpressure_has_deadline},
      {"TCP reconnect and SIGPIPE", tcp_reconnects_without_sigpipe},
      {"TCP listener lifecycle", tcp_listener_reaccepts_and_close_cancels},
      {"TCP peer-close reconnect policy", tcp_peer_close_respects_reconnect_policy},
      {"tcp end-to-end", tcp_end_to_end},
      {"TCP mixed-version interoperability", tcp_mixed_version_interoperability},
      {"Unix socket end-to-end", unix_socket_end_to_end},
      {"Unix socket cancellation", unix_socket_listener_is_interruptible_before_accept},
      {"Unix socket failed-frame invalidation", unix_socket_invalidates_failed_frames},
      {"observer-independent close", observer_failure_does_not_break_close},
      {"Unix socket send deadline", unix_socket_send_has_deadline},
      {"transport lifecycle stress", transport_lifecycle_stress}};
  std::string_view filter;
  std::size_t repetitions = 1;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--filter" && index + 1 < argc) {
      filter = argv[++index];
    } else if (argument == "--repeat" && index + 1 < argc) {
      try {
        repetitions = std::stoul(argv[++index]);
      } catch (const std::exception&) {
        std::cerr << "--repeat must be a positive integer\n";
        return 2;
      }
      if (repetitions == 0) {
        std::cerr << "--repeat must be positive\n";
        return 2;
      }
    } else {
      std::cerr << "usage: graphx-tests [--filter substring] [--repeat count]\n";
      return 2;
    }
  }
  int failures{};
  std::size_t selected{};
  for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
    for (const auto& [name, test] : tests) {
      if (!filter.empty() && std::string_view(name).find(filter) == std::string_view::npos)
        continue;
      ++selected;
      try {
        test();
        std::cout << "[pass] " << name;
        if (repetitions > 1) std::cout << " (iteration " << repetition + 1 << ')';
        std::cout << '\n';
      } catch (const std::exception& error) {
        ++failures;
        std::cerr << "[fail] " << name << ": " << error.what() << '\n';
      }
    }
  }
  if (selected == 0) {
    std::cerr << "no tests matched filter: " << filter << '\n';
    return 2;
  }
  return failures == 0 ? 0 : 1;
}
