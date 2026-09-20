#include "graphx/vita/processing.hpp"
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>
using namespace graphx::vita;
namespace rt = vr::runtime;
void check(bool ok) {
  if (!ok) throw std::runtime_error("processing assertion");
}
template <class F>
void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  check(rejected);
}
rt::timing::ProtocolTime time_at(std::uint64_t ordinal, std::uint32_t rate = 1000003) {
  auto c = *rt::timing::SampleTimeline::create({1000, 123}, rate);
  check(bool(c.advance(ordinal)));
  return c.time();
}
Spectrum example(std::uint32_t n, bool hann, int bin) {
  std::array<std::complex<double>, 2048> values{};
  std::array<bool, 2048> missing{};
  for (std::uint32_t i = 0; i < n; ++i)
    values[i] = std::polar(.25, 2 * std::numbers::pi * bin * i / n);
  auto s = power_spectrum({1000003, n, 0, hann}, std::span{values}.first(n),
                          std::span{missing}.first(n));
  s.stream = 2;
  s.center = 100000000;
  s.epoch = {1000, 123};
  s.ordinal = 137;
  s.begin = time_at(137);
  s.end = time_at(137 + n);
  return s;
}
// Inject library-owned payload buffers and decoded Context state. VITA framing is
// produced with library APIs, while sample loss/reorder is controlled by this test.
void packet(SampleAssembler& assembler, std::uint64_t ordinal, std::size_t pairs,
            std::uint8_t count,
            vr::profiles::iq::SampleFrame frame = vr::profiles::iq::SampleFrame::middle,
            bool valid = true, std::uint64_t center = 100000000) {
  struct Storage {
    alignas(64) std::array<std::byte, 4096> bytes{};
  };
  auto storage = std::make_shared<Storage>();
  for (std::size_t i = 0; i < pairs; ++i) {
    storage->bytes[4 * i] = std::byte{0x20};
  }
  vr::memory::BufferSpec spec{storage, storage->bytes.data(), 4096, 1, 64};
  auto pool = vr::memory::ExternalPool::create(std::span{&spec, 1});
  check(bool(pool));
  auto lease = pool->acquire({4096, 64});
  check(bool(lease));
  check(bool(lease->set_size(pairs * 4)));
  vr::memory::RxEnvelope envelope;
  auto index = envelope.add_buffer(std::move(*lease));
  check(bool(index));
  check(bool(envelope.append_payload({*index, 0, pairs * 4})));
  auto t = time_at(ordinal);
  vr::codec::Envelope e;
  e.stream_id = 1;
  e.class_id = vr::codec::ClassId{0xffffff, 0, 0, 0};
  e.timestamp = {vr::codec::Tsi::utc, vr::codec::Tsf::picoseconds,
                 static_cast<std::uint32_t>(t.seconds), t.picoseconds};
  e.trailer = true;
  e.packet_count = count;
  std::array<std::byte, 4128> wire{};
  auto encoded = vr::codec::encode_envelope(e, std::span{storage->bytes}.first(pairs * 4),
                                            vr::profiles::iq::graphx_trailer(frame), wire);
  check(bool(encoded));
  auto good = wire;
  e.stream_id = 2;
  auto wrong = vr::codec::encode_envelope(e, std::span{storage->bytes}.first(pairs * 4),
                                          vr::profiles::iq::graphx_trailer(frame), wire);
  check(bool(wrong));
  check(!assembler.observe(std::span{wire}.first(*wrong)));
  check(assembler.observe(std::span{good}.first(*encoded)));
  rt::context::MetadataSnapshot state;
  state.confidence = rt::context::Confidence::known;
  state.valid_data = valid;
  state.state.fields[1] = {vr::SampleRate::id, *vr::Hertz::from_integer(1000003),
                           rt::Validity::known};
  state.state.fields[4] = {vr::RFReferenceFrequency::id, *vr::Hertz::from_integer(center),
                           rt::Validity::known};
  rt::context::BorrowedSignalRx rx(envelope, state, t);
  assembler.receive(rx);
}
void drain(SampleAssembler& a) {
  std::this_thread::sleep_for(std::chrono::milliseconds(12));
  a.progress();
}
int main(int argc, char** argv) try {
  for (auto n : {64u, 128u, 256u, 512u, 1024u, 2048u})
    for (bool hann : {false, true})
      for (int sign : {-1, 1}) {
        auto s = example(n, hann, sign * 7);
        auto peak = n / 2 + sign * 7;
        check(std::abs(s.power[peak] - .0625) < 1e-7);
        check(std::abs(*detect(s) - (s.center + sign * 7.0 * s.rate / n)) < 1e-6);
        if (hann) {
          check(std::abs(s.power[peak - 1] - .015625) < 1e-7);
          check(std::abs(s.power[peak + 1] - .015625) < 1e-7);
        } else
          check(s.power[peak - 1] < 1e-20);
        auto wire = encode_spectrum(s);
        check(wire.size() == 132 + n / 4 + 4 * n && wire.size() + 28 <= 9000);
        check(decode_spectrum(wire).power == s.power);
        if (argc == 2 && !hann && sign == 1) {
          std::ofstream out(std::string(argv[1]) + "/" + std::to_string(n) + ".bin",
                            std::ios::binary);
          out.write(reinterpret_cast<const char*>(wire.data()), wire.size());
          check(bool(out));
        }
      }
  auto s = example(64, false, 3);
  auto wire = encode_spectrum(s);
  for (auto offset :
       {0u, 4u, 6u, 8u, 12u, 20u, 24u, 28u, 29u, 30u, 31u, 40u, 56u, 72u, 80u, 84u, 104u, 129u}) {
    auto bad = wire;
    bad[offset] = std::byte{255};
    rejects([&] { decode_spectrum(bad); });
  }
  auto bad = wire;
  bad.resize(bad.size() - 1);
  rejects([&] { decode_spectrum(bad); });
  bad = wire;
  bad[132 + 64 / 4] = std::byte{0x7f};
  bad[133 + 64 / 4] = std::byte{0xc0};
  rejects([&] { decode_spectrum(bad); });
  s.power.assign(64, 0);
  check(!detect(s));
  s.power[3] = s.power[5] = 1;
  check(*detect(s) == s.center + (3. - 32) * s.rate / 64);
  s.gaps[0] = std::byte{0x80};
  --s.valid;
  check(!detect(s));
  for (auto overlap : {0u, 50u, 75u}) {
    std::vector<Spectrum> results;
    SampleAssembler a(1, {1000003, 64, overlap, false},
                      [&](Spectrum v) { results.push_back(std::move(v)); });
    // Out-of-order short packets, repeated packet, adjacent bursts: no false gaps.
    packet(a, 17, 47, 1, vr::profiles::iq::SampleFrame::final);
    packet(a, 0, 17, 0, vr::profiles::iq::SampleFrame::first);
    packet(a, 0, 17, 0);
    drain(a);
    packet(a, 64, 64, 2, vr::profiles::iq::SampleFrame::single);
    a.progress();
    check(results.size() == 1 + 64 / (64 * (100 - overlap) / 100));
    check(a.duplicates == 1 && a.invalid == 0 && a.sequence_gaps == 0);
    for (auto& r : results) {
      check(r.valid == 64);
      check(r.begin == time_at(r.ordinal));
      check(r.end == time_at(r.ordinal + 64));
    }
    check(results.front().boundaries[0] == std::byte{0x80} &&
          results.front().boundaries[8] == std::byte{0x80});
  }
  std::vector<Spectrum> results;
  SampleAssembler a(1, {1000003, 64, 0, false},
                    [&](Spectrum v) { results.push_back(std::move(v)); });
  packet(a, 0, 32, 0);
  drain(a);
  packet(a, 48, 16, 2);
  a.progress();
  check(results.empty());
  drain(a);
  check(results.size() == 1 && results[0].valid == 48 && results[0].sequence_gaps == 1 &&
        !detect(results[0]));
  for (int i = 4; i < 6; ++i) check(results[0].gaps[i] == std::byte{255});
  packet(a, 64, 64, 3, vr::profiles::iq::SampleFrame::middle, false);
  drain(a);
  check(a.invalid == 1);
  packet(a, 64, 64, 3, vr::profiles::iq::SampleFrame::middle, true, 1);
  drain(a);
  check(a.invalid == 2);
  packet(a, 1000003000, 64, 4);
  drain(a);
  check(results.size() == 2 && a.skipped == 1000003000 - 64);
  check(results.back().begin == time_at(1000003000));
  // Saturation bounds input storage; excess work is counted and cannot create output.
  SampleAssembler full(1, {1000003, 64, 0, false}, [](Spectrum) {});
  for (unsigned i = 0; i < 40; ++i) packet(full, 1024 * i, 1024, i % 16);
  check(full.invalid == 8);
  drain(full);
  std::vector<Spectrum> initial_loss;
  SampleAssembler seeded(1, {1000003, 64, 0, false},
                         [&](Spectrum v) { initial_loss.push_back(std::move(v)); });
  seeded.start_epoch({1000, 123});
  packet(seeded, 17, 47, 1);
  drain(seeded);
  packet(seeded, 64, 64, 2);
  seeded.progress();
  check(initial_loss.size() == 2 && initial_loss[0].valid == 47 && initial_loss[1].valid == 64 &&
        initial_loss[1].begin == time_at(64));
  std::vector<Spectrum> initial_gap;
  SampleAssembler missing_first(1, {1000003, 64, 0, false}, [&](Spectrum v) {
    check(!detect(v));
    initial_gap.push_back(std::move(v));
  });
  missing_first.start_epoch({1000, 123});
  packet(missing_first, 96, 32, 1);
  drain(missing_first);
  check(initial_gap.size() == 2 && initial_gap[0].valid == 0 && initial_gap[1].valid == 32);
  std::vector<Spectrum> changed;
  SampleAssembler tuning(1, {1000003, 64, 0, false},
                         [&](Spectrum v) { changed.push_back(std::move(v)); });
  tuning.start_epoch({1000, 123});
  packet(tuning, 0, 32, 0);
  drain(tuning);
  packet(tuning, 32, 64, 1, vr::profiles::iq::SampleFrame::middle, true, 100010000);
  tuning.progress();
  check(changed.size() == 1 && changed[0].center == 100010000 && changed[0].begin == time_at(32) &&
        changed[0].discontinuities == 32);
  packet(tuning, 0, 16, 0);
  drain(tuning);
  check(tuning.duplicates == 1 && changed.size() == 1);
  packet(tuning, 96, 64, 2, vr::profiles::iq::SampleFrame::middle, true, 100010000);
  tuning.progress();
  check(changed.size() == 2 && changed[1].begin == time_at(96));
  std::cout << "all FFT lengths/windows/signs, exact wire, malformed spectra, overlap, burst "
               "boundaries, loss, reorder, duplicates, short packets, non-divisor time, outage and "
               "saturation passed\n";
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
