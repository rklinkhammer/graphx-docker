#pragma once
#include <SoapySDR/Device.hpp>
#include <cstdint>
#include <span>
namespace graphx::vita {
struct Settings {
  double center{100000000}, rate{1000000}, bandwidth{800000}, gain{};
};
// Single-owner device: the radio serializes all control and acquisition operations.
class VirtualDevice final : public SoapySDR::Device {
 public:
  explicit VirtualDevice(double signal, double amplitude = 0.25, double phase = 0);
  static void validate(const Settings& settings);
  void skip_samples(std::uint64_t count);
  void configure(const Settings& settings);
  const Settings& settings() const { return settings_; }
  std::uint64_t clipped() const { return clipped_; }
  size_t getNumChannels(int direction) const override;
  std::vector<std::string> getStreamFormats(int direction, size_t channel) const override;
  SoapySDR::Stream* setupStream(int direction, const std::string& format,
                                const std::vector<size_t>& channels = {},
                                const SoapySDR::Kwargs& args = {}) override;
  void closeStream(SoapySDR::Stream* stream) override;
  int activateStream(SoapySDR::Stream* stream, int flags = 0, long long time = 0,
                     size_t count = 0) override;
  int deactivateStream(SoapySDR::Stream* stream, int flags = 0, long long time = 0) override;
  int readStream(SoapySDR::Stream* stream, void* const* buffers, size_t count, int& flags,
                 long long& time, long timeout = 100000) override;
  void setFrequency(int direction, size_t channel, double value,
                    const SoapySDR::Kwargs& args = {}) override;
  double getFrequency(int direction, size_t channel) const override;
  void setSampleRate(int direction, size_t channel, double value) override;
  double getSampleRate(int direction, size_t channel) const override;
  void setBandwidth(int direction, size_t channel, double value) override;
  double getBandwidth(int direction, size_t channel) const override;
  void setGain(int direction, size_t channel, double value) override;
  double getGain(int direction, size_t channel) const override;
  SoapySDR::RangeList getFrequencyRange(int direction, size_t channel) const override;
  SoapySDR::RangeList getSampleRateRange(int direction, size_t channel) const override;
  SoapySDR::RangeList getBandwidthRange(int direction, size_t channel) const override;
  SoapySDR::Range getGainRange(int direction, size_t channel) const override;

 private:
  void check(SoapySDR::Stream* stream) const;
  static void channel(int direction, size_t channel);
  Settings settings_;
  double signal_, amplitude_, phase_{};
  std::uint64_t clipped_{}, sample_{};
  bool opened_{}, active_{};
};
int run_radio(int argc, char** argv);
}  // namespace graphx::vita
