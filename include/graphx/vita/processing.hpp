#pragma once
#include "graphx/vita/runtime.hpp"
#include <complex>
#include <deque>
#include <functional>
namespace graphx::vita {
struct ProcessingConfig {
  std::uint32_t rate{1000000}, size{1024}, overlap{0};
  bool hann{};
  std::uint32_t hop() const { return size * (100 - overlap) / 100; }
  void validate() const;
};
struct Spectrum {
  std::uint32_t stream{}, sequence{}, rate{}, size{}, hop{}, valid{};
  bool hann{};
  std::uint64_t center{}, discontinuities{}, ordinal{}, sequence_gaps{};
  vr::runtime::timing::ProtocolTime begin{}, end{}, epoch{};
  std::vector<float> power;
  std::vector<std::byte> gaps, boundaries;
};
std::size_t spectrum_bytes(std::size_t size);
std::vector<std::byte> encode_spectrum(const Spectrum&);
Spectrum decode_spectrum(std::span<const std::byte>);
std::optional<double> detect(const Spectrum&);
Spectrum power_spectrum(ProcessingConfig, std::span<const std::complex<double>>,
                        std::span<const bool> missing);
// One bounded application sample assembler; all VITA decoding is library-owned.
class SampleAssembler {
 public:
  using Emit = std::function<void(Spectrum)>;
  SampleAssembler(std::uint32_t sid, ProcessingConfig config, Emit emit);
  void start_epoch(vr::runtime::timing::ProtocolTime);
  bool observe(vr::Bytes);
  void receive(const vr::runtime::context::BorrowedSignalRx&) noexcept;
  void progress();
  std::uint64_t invalid{}, duplicates{}, skipped{}, sequence_gaps{};

 private:
  struct Packet {
    vr::runtime::timing::ProtocolTime time;
    std::uint64_t center{};
    std::vector<std::complex<double>> samples;
    std::uint8_t count{};
    bool first{}, final{};
    std::chrono::steady_clock::time_point arrival;
  };
  struct Sample {
    std::complex<double> value;
    bool missing{}, first{}, final{};
  };
  std::uint32_t sid_, sequence_{};
  ProcessingConfig config_;
  Emit emit_;
  struct Hint {
    vr::runtime::timing::ProtocolTime time;
    std::uint8_t count{};
    bool first{}, final{};
  };
  std::deque<Hint> hints_;
  std::optional<std::uint8_t> count_;
  std::deque<Packet> pending_;
  std::deque<Sample> samples_;
  std::optional<vr::runtime::timing::ProtocolTime> epoch_;
  std::uint64_t center_{}, ordinal_{}, window_{};
  void append(Sample);
  void consume(Packet);
};
int run_processor(int, char**);
int run_detector(int, char**);
}  // namespace graphx::vita
