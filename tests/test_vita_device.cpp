#include "graphx/vita/virtual_device.hpp"
#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using graphx::vita::VirtualDevice;
void require(bool value) {
  if (!value) throw std::runtime_error("device test assertion");
}
int main() try {
  VirtualDevice first(100050000), second(100100000);
  SoapySDR::Device& device = first;
  require(device.getNumChannels(SOAPY_SDR_RX) == 1);
  require(device.getNumChannels(SOAPY_SDR_TX) == 0);
  require(device.getFrequencyRange(SOAPY_SDR_RX, 0).front().minimum() == 1000000);
  require(device.getSampleRateRange(SOAPY_SDR_RX, 0).front().maximum() == 2000000);
  require(device.getGainRange(SOAPY_SDR_RX, 0).minimum() == -60);
  auto* stream = device.setupStream(SOAPY_SDR_RX, "CS16");
  std::array<std::int16_t, 2048> data{};
  void* buffers[] = {data.data()};
  int flags{};
  long long time{};
  require(device.readStream(stream, buffers, 10, flags, time) == SOAPY_SDR_TIMEOUT);
  require(device.activateStream(stream) == 0);
  require(device.readStream(stream, buffers, 10, flags, time) == 10);
  require(data[0] == 8192 && data[1] == 0);
  require(std::abs(data[2] - 7791) <= 1 && std::abs(data[3] - 2531) <= 1);
  require(device.readStream(stream, buffers, 10, flags, time) == 10);
  require(data[0] == -8192 && data[1] == 0 && time == 10000);
  device.setGain(SOAPY_SDR_RX, 0, 60);
  require(device.readStream(stream, buffers, 10, flags, time) == 10);
  require(first.clipped() > 0 && second.clipped() == 0);
  device.setFrequency(SOAPY_SDR_RX, 0, 101000000);
  require(device.readStream(stream, buffers, 10, flags, time) == 10);
  for (size_t n = 0; n < 20; ++n) require(data[n] == 0);
  require(second.getFrequency(SOAPY_SDR_RX, 0) == 100000000);
  auto old = first.settings();
  try {
    first.configure({100000000, 0, 800000, 0});
    require(false);
  } catch (const std::invalid_argument&) {
  }
  require(first.settings().center == old.center);
  try {
    device.setGain(SOAPY_SDR_RX, 0, std::numeric_limits<double>::infinity());
    require(false);
  } catch (const std::invalid_argument&) {
  }
  require(device.deactivateStream(stream) == 0);
  require(device.readStream(stream, buffers, 10, flags, time) == SOAPY_SDR_TIMEOUT);
  device.closeStream(stream);
  std::cout
      << "virtual device: API, waveform, phase, gain, passband, isolation and bounds passed\n";
}

catch (const std::exception& error) {
  std::cerr << error.what() << "\n";
  return 1;
}
