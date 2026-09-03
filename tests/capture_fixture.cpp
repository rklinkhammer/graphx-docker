#include "graphx/capture.hpp"
#include "graphx/envelope.hpp"
#include "graphx/framing.hpp"

#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: graphx-capture-fixture OUTPUT.pcapng\n";
    return 64;
  }
  try {
    graphx::PcapngCaptureSink capture(argv[1], 16 * 1024 * 1024 + 4, 1024 * 1024, 10);
    graphx::Envelope v1{.sequence = 7,
                        .timestamp_ns = 1'700'000'000'000'000'000,
                        .type = "legacy.sample",
                        .trace_id = "legacy-trace",
                        .attributes = {{"source", "fixture"}},
                        .payload = "old",
                        .wire_version = graphx::kEnvelopeWireVersion1};
    graphx::Envelope v2{.sequence = 42,
                        .timestamp_ns = 1'700'000'000'100'000'000,
                        .type = "sample",
                        .trace_id = "0123456789abcdef0123456789abcdef",
                        .attributes = {{"source", "fixture"}, {"status", "ok"}},
                        .payload = "payload",
                        .message_id = "00112233445566778899aabbccddeeff",
                        .parent_message_id = "ffeeddccbbaa99887766554433221100",
                        .wire_version = graphx::kEnvelopeWireVersion2};
    const auto timestamp =
        std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
    capture.record_frame("samples", graphx::frame(graphx::serialize(v1)), timestamp,
                         {.direction = graphx::CaptureSink::Direction::sent,
                          .sequence = v1.sequence,
                          .wire_version = v1.wire_version,
                          .trace_id = v1.trace_id,
                          .type = v1.type});
    capture.record_frame("samples", graphx::frame(graphx::serialize(v2)), timestamp,
                         {.direction = graphx::CaptureSink::Direction::received,
                          .sequence = v2.sequence,
                          .wire_version = v2.wire_version,
                          .message_id = v2.message_id,
                          .parent_message_id = v2.parent_message_id,
                          .trace_id = v2.trace_id,
                          .type = v2.type});
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
