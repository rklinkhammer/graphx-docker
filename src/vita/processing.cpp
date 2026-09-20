#include "graphx/vita/processing.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <limits>
#include <stdexcept>
namespace graphx::vita {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
namespace {
void require(bool yes) {
  if (!yes) throw std::invalid_argument("invalid power spectrum");
}
void put(std::vector<std::byte>& out, std::size_t offset, std::uint64_t value, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i)
    out.at(offset + i) = std::byte((value >> (8 * (count - i - 1))) & 255);
}
std::uint64_t get(std::span<const std::byte> in, std::size_t offset, std::size_t count) {
  require(offset + count <= in.size());
  std::uint64_t result = 0;
  for (std::size_t i = 0; i < count; ++i)
    result = (result << 8) | std::to_integer<unsigned>(in[offset + i]);
  return result;
}
void validate(const Spectrum& s) {
  ProcessingConfig c{s.rate, s.size, 0, s.hann};
  require(s.hop && s.hop <= s.size && s.size % s.hop == 0);
  c.overlap = 100 - s.hop * 100 / s.size;
  c.validate();
  require(s.stream >= 1 && s.stream <= 4 && s.center >= 1000000 && s.center <= 6000000000ULL);
  require(s.valid <= s.size && s.power.size() == s.size && s.gaps.size() == s.size / 8 &&
          s.boundaries.size() == s.size / 8 + 4);
  require((std::to_integer<unsigned>(s.boundaries[s.size / 8]) & 127) == 0);
  for (std::size_t i = s.size / 8 + 1; i < s.boundaries.size(); ++i)
    require(s.boundaries[i] == std::byte{});
  auto timeline = vr::runtime::timing::SampleTimeline::create(s.epoch, s.rate);
  require(bool(timeline));
  require(bool(timeline->advance(s.ordinal)));
  require(timeline->time() == s.begin);
  require(bool(timeline->advance(s.size)));
  require(timeline->time() == s.end);
  std::uint32_t missing = 0;
  for (auto b : s.gaps) missing += std::popcount(std::to_integer<unsigned>(b));
  require(s.valid + missing == s.size);
  for (auto power : s.power) require(std::isfinite(power) && power >= 0);
}
}  // namespace
void ProcessingConfig::validate() const {
  require(rate >= 1000 && rate <= 2000000 && size >= 64 && size <= 2048 &&
          std::has_single_bit(size));
  require(overlap == 0 || overlap == 50 || overlap == 75);
}
std::size_t spectrum_bytes(std::size_t n) {
  require(n >= 64 && n <= 2048 && std::has_single_bit(n));
  return 132 + n / 4 + 4 * n;
}
Spectrum power_spectrum(ProcessingConfig c, std::span<const std::complex<double>> samples,
                        std::span<const bool> missing) {
  c.validate();
  require(samples.size() == c.size && missing.size() == c.size);
  Spectrum result;
  result.rate = c.rate;
  result.size = c.size;
  result.hop = c.hop();
  result.hann = c.hann;
  result.power.resize(c.size);
  result.gaps.resize(c.size / 8);
  result.boundaries.resize(c.size / 8 + 4);
  std::vector<std::complex<double>> a(c.size);
  double scale = 0;
  for (std::size_t i = 0; i < c.size; ++i) {
    double w = c.hann ? .5 - .5 * std::cos(2 * std::numbers::pi * i / c.size) : 1;
    require(std::isfinite(samples[i].real()) && std::isfinite(samples[i].imag()));
    a[i] = missing[i] ? 0.0 : samples[i] * w;
    scale += w;
    if (missing[i])
      result.gaps[i / 8] |= std::byte(1u << (7 - i % 8));
    else
      ++result.valid;
  }
  for (std::size_t i = 1, j = 0; i < c.size; ++i) {
    std::size_t bit = c.size >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t length = 2; length <= c.size; length *= 2) {
    auto root = std::polar(1.0, -2 * std::numbers::pi / length);
    for (std::size_t base = 0; base < c.size; base += length) {
      std::complex<double> w = 1;
      for (std::size_t k = 0; k < length / 2; ++k) {
        auto u = a[base + k], v = w * a[base + k + length / 2];
        a[base + k] = u + v;
        a[base + k + length / 2] = u - v;
        w *= root;
      }
    }
  }
  for (std::size_t i = 0; i < c.size; ++i)
    result.power[i] = static_cast<float>(std::norm(a[(i + c.size / 2) % c.size]) / (scale * scale));
  return result;
}
std::vector<std::byte> encode_spectrum(const Spectrum& s) {
  validate(s);
  std::vector<std::byte> out(spectrum_bytes(s.size));
  put(out, 0, 0x47585032, 4);
  put(out, 4, 1, 2);
  put(out, 6, 128, 2);
  put(out, 8, out.size(), 4);
  put(out, 12, s.stream, 4);
  put(out, 16, s.sequence, 4);
  put(out, 20, s.size, 4);
  put(out, 24, s.hop, 4);
  put(out, 28, s.hann, 1);
  put(out, 29, 1, 1);
  put(out, 30, 1, 1);
  put(out, 32, s.center, 8);
  put(out, 40, s.rate, 8);
  put(out, 48, s.begin.seconds, 8);
  put(out, 56, s.begin.picoseconds, 8);
  put(out, 64, s.end.seconds, 8);
  put(out, 72, s.end.picoseconds, 8);
  put(out, 80, s.valid, 4);
  put(out, 84, s.size - s.valid, 4);
  put(out, 88, s.discontinuities, 8);
  put(out, 96, s.epoch.seconds, 8);
  put(out, 104, s.epoch.picoseconds, 8);
  put(out, 112, s.ordinal, 8);
  put(out, 120, s.sequence_gaps, 8);
  std::copy(s.gaps.begin(), s.gaps.end(), out.begin() + 128);
  std::copy(s.boundaries.begin(), s.boundaries.end(), out.begin() + 128 + s.size / 8);
  for (std::size_t i = 0; i < s.size; ++i)
    put(out, 132 + s.size / 4 + 4 * i, std::bit_cast<std::uint32_t>(s.power[i]), 4);
  return out;
}
Spectrum decode_spectrum(std::span<const std::byte> in) {
  require(in.size() >= 128 && get(in, 0, 4) == 0x47585032 && get(in, 4, 2) == 1 &&
          get(in, 6, 2) == 128 && get(in, 8, 4) == in.size());
  Spectrum s;
  s.stream = get(in, 12, 4);
  s.sequence = get(in, 16, 4);
  s.size = get(in, 20, 4);
  s.hop = get(in, 24, 4);
  require(in.size() == spectrum_bytes(s.size));
  require(get(in, 28, 1) <= 1 && get(in, 29, 1) == 1 && get(in, 30, 1) == 1 && get(in, 31, 1) == 0);
  s.hann = get(in, 28, 1);
  s.center = get(in, 32, 8);
  auto rate = get(in, 40, 8);
  require(rate <= 2000000);
  s.rate = rate;
  s.begin = {get(in, 48, 8), get(in, 56, 8)};
  s.end = {get(in, 64, 8), get(in, 72, 8)};
  s.valid = get(in, 80, 4);
  require(get(in, 84, 4) == s.size - s.valid);
  s.discontinuities = get(in, 88, 8);
  s.epoch = {get(in, 96, 8), get(in, 104, 8)};
  s.ordinal = get(in, 112, 8);
  s.sequence_gaps = get(in, 120, 8);
  s.boundaries.assign(in.begin() + 128 + s.size / 8, in.begin() + 132 + s.size / 4);
  s.gaps.assign(in.begin() + 128, in.begin() + 128 + s.size / 8);
  s.power.resize(s.size);
  for (std::size_t i = 0; i < s.size; ++i)
    s.power[i] =
        std::bit_cast<float>(static_cast<std::uint32_t>(get(in, 132 + s.size / 4 + 4 * i, 4)));
  validate(s);
  return s;
}
std::optional<double> detect(const Spectrum& s) {
  validate(s);
  if (s.valid != s.size) return {};
  auto peak = std::max_element(s.power.begin(), s.power.end());
  if (*peak == 0) return {};
  return static_cast<double>(s.center) +
         (std::distance(s.power.begin(), peak) - static_cast<double>(s.size) / 2) * s.rate / s.size;
}
}  // namespace graphx::vita
namespace graphx::vita {
SampleAssembler::SampleAssembler(std::uint32_t sid, ProcessingConfig c, Emit emit)
    : sid_(sid), config_(c), emit_(std::move(emit)) {
  c.validate();
}
void SampleAssembler::start_epoch(vr::runtime::timing::ProtocolTime time) {
  require(vr::runtime::timing::valid(time));
  epoch_ = time;
  ordinal_ = window_ = center_ = 0;
  samples_.clear();
  pending_.clear();
  hints_.clear();
  count_.reset();
}
void SampleAssembler::append(Sample value) {
  samples_.push_back(value);
  ++ordinal_;
  if (samples_.size() < config_.size) return;
  std::array<std::complex<double>, 2048> values{};
  std::array<bool, 2048> missing{};
  for (std::size_t i = 0; i < config_.size; ++i) {
    values[i] = samples_[i].value;
    missing[i] = samples_[i].missing;
  }
  auto spectrum = power_spectrum(config_, std::span{values}.first(config_.size),
                                 std::span{missing}.first(config_.size));
  spectrum.epoch = *epoch_;
  spectrum.ordinal = window_;
  spectrum.stream = sid_;
  spectrum.sequence = sequence_++;
  spectrum.center = center_;
  spectrum.discontinuities = skipped;
  spectrum.sequence_gaps = sequence_gaps;
  auto boundary = [&](std::size_t i) {
    spectrum.boundaries[i / 8] |= std::byte(1u << (7 - i % 8));
  };
  for (std::size_t i = 0; i < config_.size; ++i) {
    if (samples_[i].first) boundary(i);
    if (samples_[i].final) boundary(i + 1);
  }
  auto clock = *vr::runtime::timing::SampleTimeline::create(*epoch_, config_.rate);
  if (!clock.advance(window_)) throw std::runtime_error("sample time overflow");
  spectrum.begin = clock.time();
  if (!clock.advance(config_.size)) throw std::runtime_error("sample time overflow");
  spectrum.end = clock.time();
  emit_(std::move(spectrum));
  for (std::uint32_t i = 0; i < config_.hop(); ++i) samples_.pop_front();
  window_ += config_.hop();
}
void SampleAssembler::consume(Packet packet) {
  if (!epoch_) epoch_ = packet.time;
  if (packet.time < *epoch_) {
    ++duplicates;
    return;
  }
  auto sec = packet.time.seconds - epoch_->seconds;
  if (sec > static_cast<std::uint64_t>(INT64_MAX) / config_.rate) {
    ++invalid;
    return;
  }
  auto ps = static_cast<std::int64_t>(packet.time.picoseconds) -
            static_cast<std::int64_t>(epoch_->picoseconds);
  auto at = static_cast<std::int64_t>(sec * config_.rate) +
            (ps * config_.rate + (ps >= 0 ? 500000000000LL : -500000000000LL)) / 1000000000000LL;
  if (at < static_cast<std::int64_t>(ordinal_)) {
    ++duplicates;
    return;
  }
  auto clock = *vr::runtime::timing::SampleTimeline::create(*epoch_, config_.rate);
  if (!clock.advance(at) || clock.time() != packet.time) {
    ++invalid;
    return;
  }
  if (count_ && packet.count != ((*count_ + 1) & 15)) ++sequence_gaps;
  count_ = packet.count;
  auto gap = static_cast<std::uint64_t>(at) - ordinal_;
  bool changed = center_ != 0 && center_ != packet.center;
  center_ = packet.center;
  if (gap > 2 * config_.size || changed) {
    skipped += gap + samples_.size();
    samples_.clear();
    ordinal_ = window_ = at;
  } else
    for (std::uint64_t i = 0; i < gap; ++i) append({{}, true});
  for (std::size_t i = 0; i < packet.samples.size(); ++i)
    append({packet.samples[i], false, packet.first && i == 0,
            packet.final && i + 1 == packet.samples.size()});
}
bool SampleAssembler::observe(vr::Bytes wire) {
  auto view = vr::codec::decode_envelope(wire);
  if (!view || view->envelope.stream_id != sid_) return false;
  const auto& e = view->envelope;
  if (e.type == vr::codec::PacketType::context) return true;
  if (e.type != vr::codec::PacketType::signal || e.timestamp.tsi != vr::codec::Tsi::utc ||
      e.timestamp.tsf != vr::codec::Tsf::picoseconds || !view->trailer)
    return false;
  using Frame = vr::profiles::iq::SampleFrame;
  std::optional<Frame> frame;
  for (auto f : {Frame::single, Frame::first, Frame::middle, Frame::final})
    if (*view->trailer == vr::profiles::iq::graphx_trailer(f)) frame = f;
  if (!frame) return false;
  if (hints_.size() == 128) {
    hints_.pop_front();
    ++invalid;
  }
  hints_.push_back({{e.timestamp.integer, e.timestamp.fractional},
                    e.packet_count,
                    *frame == Frame::single || *frame == Frame::first,
                    *frame == Frame::single || *frame == Frame::final});
  return true;
}
void SampleAssembler::receive(const vr::runtime::context::BorrowedSignalRx& rx) noexcept {
  try {
    if (rx.sample_time.seconds > UINT32_MAX || rx.sample_time.picoseconds >= 1000000000000ULL) {
      ++invalid;
      return;
    }
    if (rx.metadata.confidence != vr::runtime::context::Confidence::known ||
        !rx.metadata.valid_data) {
      ++invalid;
      return;
    }
    auto rate = std::get<vr::Hertz>(rx.metadata.state.fields[1].value).q20;
    if (rate != static_cast<std::int64_t>(config_.rate) * 1048576) {
      ++invalid;
      return;
    }
    auto hint = std::find_if(hints_.begin(), hints_.end(),
                             [&](const auto& h) { return h.time == rx.sample_time; });
    if (hint == hints_.end()) {
      ++invalid;
      return;
    }
    auto center = std::get<vr::Hertz>(rx.metadata.state.fields[4].value).q20;
    if (center < 1000000LL * 1048576 || center > 6000000000LL * 1048576 || center % 1048576) {
      ++invalid;
      return;
    }
    Packet packet{rx.sample_time,
                  static_cast<std::uint64_t>(center / 1048576),
                  {},
                  hint->count,
                  hint->first,
                  hint->final,
                  std::chrono::steady_clock::now()};
    hints_.erase(hint);
    for (std::size_t f = 0; f < rx.fragment_count(); ++f) {
      auto bytes = rx.fragment(f);
      if (!bytes) throw std::runtime_error("sample fragment");
      auto view = vr::codec::SampleView<std::int16_t>::create(*bytes);
      if (!view) throw std::runtime_error("sample view");
      for (std::size_t i = 0; i < view->size(); ++i) {
        auto iq = *view->at(i);
        packet.samples.emplace_back(iq.i / 32768.0, iq.q / 32768.0);
      }
    }
    if (packet.samples.empty() || packet.samples.size() > 1024 || pending_.size() == 32) {
      ++invalid;
      return;
    }
    auto pos = std::lower_bound(pending_.begin(), pending_.end(), packet.time,
                                [](const auto& a, const auto& t) { return a.time < t; });
    if (pos != pending_.end() && pos->time == packet.time) {
      ++duplicates;
      return;
    }
    pending_.insert(pos, std::move(packet));
  } catch (...) {
    ++invalid;
  }
}
void SampleAssembler::progress() {
  auto now = std::chrono::steady_clock::now();
  while (!pending_.empty()) {
    auto expected = epoch_ ? vr::runtime::timing::SampleTimeline::create(*epoch_, config_.rate)
                           : std::unexpected(vr::Error{vr::ErrorCode::invalid_state});
    bool contiguous = false;
    if (expected && expected->advance(ordinal_))
      contiguous = expected->time() == pending_.front().time;
    if (!contiguous && now - pending_.front().arrival < std::chrono::milliseconds(10)) break;
    auto packet = std::move(pending_.front());
    pending_.pop_front();
    consume(std::move(packet));
  }
}
}  // namespace graphx::vita
