#include "graphx/vita/virtual_device.hpp"
#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace graphx::vita {
namespace {
void range(double value, double minimum, double maximum) {
  if (!std::isfinite(value) || value < minimum || value > maximum)
    throw std::invalid_argument("E_RADIO_RANGE: unsupported setting");
}
}  // namespace
VirtualDevice::VirtualDevice(double signal, double amplitude, double phase)
    : signal_(signal), amplitude_(amplitude), phase_(phase), initial_phase_(phase) {
  range(signal, 1e6, 6e9);
  range(amplitude, 0, 1);
  range(phase, 0, 2 * std::numbers::pi);
}
void VirtualDevice::validate(const Settings& s) {
  range(s.center, 1e6, 6e9);
  range(s.rate, 1000, 2000000);
  if (s.rate != std::floor(s.rate))
    throw std::invalid_argument("E_RADIO_RATE: integral rate required");
  range(s.bandwidth, 1, s.rate);
  range(s.gain, -60, 60);
}
void VirtualDevice::begin_epoch() noexcept {
  sample_ = 0;
  phase_ = initial_phase_;
}
void VirtualDevice::skip_samples(std::uint64_t count) {
  const long double cycles =
      static_cast<long double>(signal_ - settings_.center) * count / settings_.rate;
  phase_ = std::remainder(
      phase_ + static_cast<double>(std::remainder(cycles, 1.0L)) * 2 * std::numbers::pi,
      2 * std::numbers::pi);
  sample_ += count;
}
void VirtualDevice::configure(const Settings& settings) {
  validate(settings);
  settings_ = settings;
}
void VirtualDevice::channel(int direction, size_t index) {
  if (direction != SOAPY_SDR_RX || index != 0) throw std::invalid_argument("E_RADIO_CHANNEL");
}
size_t VirtualDevice::getNumChannels(int direction) const {
  return direction == SOAPY_SDR_RX ? 1 : 0;
}
std::vector<std::string> VirtualDevice::getStreamFormats(int direction, size_t index) const {
  channel(direction, index);
  return {"CS16"};
}
SoapySDR::Stream* VirtualDevice::setupStream(int direction, const std::string& format,
                                             const std::vector<size_t>& channels,
                                             const SoapySDR::Kwargs& args) {
  channel(direction, channels.empty() ? 0 : channels.front());
  if (opened_ || channels.size() > 1 || format != "CS16" || !args.empty())
    throw std::invalid_argument("E_RADIO_STREAM");
  opened_ = true;
  return reinterpret_cast<SoapySDR::Stream*>(this);
}
void VirtualDevice::check(SoapySDR::Stream* stream) const {
  if (!opened_ || stream != reinterpret_cast<const SoapySDR::Stream*>(this))
    throw std::invalid_argument("E_RADIO_STREAM");
}
void VirtualDevice::closeStream(SoapySDR::Stream* stream) {
  check(stream);
  active_ = false;
  opened_ = false;
}
int VirtualDevice::activateStream(SoapySDR::Stream* stream, int flags, long long time,
                                  size_t count) {
  check(stream);
  if (flags || time || count) return SOAPY_SDR_NOT_SUPPORTED;
  active_ = true;
  return 0;
}
int VirtualDevice::deactivateStream(SoapySDR::Stream* stream, int flags, long long time) {
  check(stream);
  if (flags || time) return SOAPY_SDR_NOT_SUPPORTED;
  active_ = false;
  return 0;
}
int VirtualDevice::readStream(SoapySDR::Stream* stream, void* const* buffers, size_t count,
                              int& flags, long long& time, long timeout) {
  check(stream);
  (void)timeout;
  if (!active_) return SOAPY_SDR_TIMEOUT;
  if (!buffers || !buffers[0] || count == 0 || count > 1024) return SOAPY_SDR_STREAM_ERROR;
  auto* output = static_cast<std::int16_t*>(buffers[0]);
  flags = SOAPY_SDR_HAS_TIME;
  const auto rate = static_cast<std::uint64_t>(settings_.rate);
  time =
      static_cast<long long>((sample_ / rate) * 1000000000 + (sample_ % rate) * 1000000000 / rate);
  const double offset = signal_ - settings_.center;
  const bool in_band =
      std::abs(offset) <= settings_.bandwidth / 2 && std::abs(offset) < settings_.rate / 2;
  const double amplitude = in_band ? 32767 * amplitude_ * std::pow(10.0, settings_.gain / 20) : 0;
  const auto convert = [&](double value) {
    if (value < -32768 || value > 32767) ++clipped_;
    return static_cast<std::int16_t>(std::llround(std::clamp(value, -32768.0, 32767.0)));
  };
  const double increment =
      2 * std::numbers::pi * std::remainder(offset, settings_.rate) / settings_.rate;
  for (size_t n = 0; n < count; ++n) {
    output[2 * n] = convert(amplitude * std::cos(phase_));
    output[2 * n + 1] = convert(amplitude * std::sin(phase_));
    phase_ = std::remainder(phase_ + increment, 2 * std::numbers::pi);
  }
  sample_ += count;
  return static_cast<int>(count);
}
void VirtualDevice::setFrequency(int d, size_t c, double v, const SoapySDR::Kwargs& args) {
  channel(d, c);
  if (!args.empty()) throw std::invalid_argument("E_RADIO_ARGS");
  auto s = settings_;
  s.center = v;
  configure(s);
}
double VirtualDevice::getFrequency(int d, size_t c) const {
  channel(d, c);
  return settings_.center;
}
void VirtualDevice::setSampleRate(int d, size_t c, double v) {
  channel(d, c);
  auto s = settings_;
  s.rate = v;
  configure(s);
}
double VirtualDevice::getSampleRate(int d, size_t c) const {
  channel(d, c);
  return settings_.rate;
}
void VirtualDevice::setBandwidth(int d, size_t c, double v) {
  channel(d, c);
  auto s = settings_;
  s.bandwidth = v;
  configure(s);
}
double VirtualDevice::getBandwidth(int d, size_t c) const {
  channel(d, c);
  return settings_.bandwidth;
}
void VirtualDevice::setGain(int d, size_t c, double v) {
  channel(d, c);
  auto s = settings_;
  s.gain = v;
  configure(s);
}
double VirtualDevice::getGain(int d, size_t c) const {
  channel(d, c);
  return settings_.gain;
}
SoapySDR::RangeList VirtualDevice::getFrequencyRange(int d, size_t c) const {
  channel(d, c);
  return {{1e6, 6e9}};
}
SoapySDR::RangeList VirtualDevice::getSampleRateRange(int d, size_t c) const {
  channel(d, c);
  return {{1000, 2000000, 1}};
}
SoapySDR::RangeList VirtualDevice::getBandwidthRange(int d, size_t c) const {
  channel(d, c);
  return {{1, 2000000}};
}
SoapySDR::Range VirtualDevice::getGainRange(int d, size_t c) const {
  channel(d, c);
  return {-60, 60};
}
}  // namespace graphx::vita
