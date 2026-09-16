#include "graphx/vita/virtual_device.hpp"
#include "graphx/node_settings.hpp"
#include "../application_observer.hpp"
#include "radio_ack.hpp"
#include "radio_context.hpp"
#include "radio_control.hpp"
#include "radio_query.hpp"
#include "radio_query_ack.hpp"
#include <type_traits>
#include "radio_data.hpp"
#include <SoapySDR/Constants.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <numbers>
#include <vector>
#include <thread>
namespace graphx::vita {
namespace {
using namespace std::chrono;
using namespace packets;
using namespace vrtgen::packing;
using Bytes = std::vector<std::uint8_t>;
using Clock = steady_clock;
volatile std::sig_atomic_t stopping{};
void stop_signal(int) { stopping = 1; }
struct Fd {
  int value;
  explicit Fd(int value_) : value(value_) {
    if (value < 0) throw std::runtime_error("socket failed");
  }
  ~Fd() { close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
sockaddr_in address(const std::string& ip, std::int64_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  if (port < 1 || port > 65535 || inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1)
    throw std::runtime_error("invalid resolved endpoint");
  return a;
}
std::uint32_t word(std::span<const std::uint8_t> b, size_t n) {
  if (n + 4 > b.size()) throw std::runtime_error("truncated control");
  return (std::uint32_t(b[n]) << 24) | (std::uint32_t(b[n + 1]) << 16) |
         (std::uint32_t(b[n + 2]) << 8) | b[n + 3];
}
// Reject unknown layouts and check every optional field extent before invoking
// upstream generated accessors, which assume a complete, structurally valid span.
void validate_control(const Bytes& b) {
  if (b.size() < 40 || b.size() > 1024 || (word(b, 0) & 65535) * 4 != b.size())
    throw std::runtime_error("control size");
  auto cif = word(b, 36);
  constexpr std::uint32_t fields = 0x28a00000;
  if (word(b, 20) & ~0xa38d7000U) throw std::runtime_error("unsupported CAM bits");
  if (cif & ~(fields | 2U)) throw std::runtime_error("unsupported CIF0");
  const bool query = ((word(b, 20) >> 23) & 3) == 0;
  size_t length = 40;
  if (query) {
    if (cif & 2U) {
      if (word(b, 40) != 64) throw std::runtime_error("unsupported query CIF1");
      length += 4;
    }
    if (cif == 0 || length != b.size()) throw std::runtime_error("query selectors");
    if (auto mismatch = RadioQuery::match(b)) throw std::runtime_error(*mismatch);
    return;
  }
  if (cif & 2U) {
    if (word(b, 40) != 64) throw std::runtime_error("unsupported CIF1");
    if (word(b, b.size() - 4) != 2 && word(b, b.size() - 4) != 3)
      throw std::runtime_error("unsupported discrete IO");
    length += 8;
  }
  if (cif & (1U << 29)) length += 8;
  if (cif & (1U << 27)) length += 8;
  if (cif & (1U << 23)) length += 4;
  if (cif & (1U << 21)) length += 8;
  if (length != b.size()) throw std::runtime_error("control layout");
  auto mismatch = ((word(b, 20) >> 23) & 3) == 0 ? RadioQuery::match(b) : RadioControl::match(b);
  if (mismatch) throw std::runtime_error(*mismatch);
}
template <class P>
Bytes bytes(P& p) {
  auto data = p.data();
  return {data.begin(), data.end()};
}
template <class P, class C>
void identity(P& p, C& command) {
  p.stream_id(command.stream_id());
  p.controllee_id(command.controllee_id());
  p.controller_id(command.controller_id());
  p.message_id(command.message_id());
  p.integer_timestamp(command.integer_timestamp());
  p.fractional_timestamp(command.fractional_timestamp());
}
struct Client {
  Fd fd;
  std::unique_ptr<SSL, decltype(&SSL_free)> ssl;
  bool ready{};
  Bytes input, output;
  Clock::time_point deadline{Clock::now() + seconds(2)};
  Client(int socket, SSL_CTX* ctx) : fd(socket), ssl(SSL_new(ctx), SSL_free) {
    if (!ssl || SSL_set_fd(ssl.get(), fd.value) != 1) throw std::runtime_error("TLS allocation");
    fcntl(fd.value, F_SETFL, O_NONBLOCK);
    SSL_set_accept_state(ssl.get());
  }
};
class Radio {
 public:
  VirtualDevice device;
  SoapySDR::Stream* stream;
  std::uint32_t id, burst;
  std::uint64_t sample{}, epoch_ps{}, epoch_sec{}, dropped{}, packets{}, udp_errors{};
  bool configured{}, running{}, armed{};
  Clock::time_point start{};
  std::map<std::uint32_t, std::pair<Bytes, Bytes>> replay;
  std::uint32_t highest{};
  Radio(std::uint32_t id_, double signal, std::uint32_t burst_, double amplitude, double phase)
      : device(signal, amplitude, phase), id(id_), burst(burst_) {
    stream = device.setupStream(SOAPY_SDR_RX, "CS16");
  }
  ~Radio() { device.closeStream(stream); }
  Bytes command(const Bytes& request) {
    validate_control(request);
    const auto mode = static_cast<ActionMode>((word(request, 20) >> 23) & 3);
    RadioControl c;
    if (mode == ActionMode::NO_ACTION) {
      // Never decode selector-only bytes with the execute decoder: it reads values.
      RadioQuery query(request);
      if (query.cam().permit_partial() || query.cam().permit_warnings() ||
          query.timing_control() != TimestampControlMode::IGNORE ||
          query.fractional_timestamp() >= 1000000000000ULL)
        throw std::runtime_error("unsupported query CAM/timestamp");
      identity(c, query);
      c.req_s(true);
    } else {
      c = RadioControl(request);
    }
    if (c.stream_id() != id || c.controllee_id() != id || c.controller_id() != 1)
      throw std::runtime_error("command identity");
    if (auto found = replay.find(c.message_id()); found != replay.end()) {
      if (found->second.first != request) throw std::runtime_error("message ID reuse");
      return found->second.second;
    }
    if (!c.message_id() || c.message_id() <= highest)
      throw std::runtime_error("expired message ID");
    Bytes response;
    std::uint32_t error_fields = word(request, 36) & 0x28a00000;
    WarningErrorFields error;
    error.field_not_executed(true);
    try {
      if (c.cam().permit_partial() || c.cam().permit_warnings() ||
          c.fractional_timestamp() >= 1000000000000ULL)
        throw std::runtime_error("unsupported CAM/timestamp");
      const auto mask = word(request, 36) & 0x28a00000;
      if (mode == ActionMode::NO_ACTION) {
        // Query packets contain selectors only, not the execute packet's values.
        if (!c.req_s() || c.timing_control() != TimestampControlMode::IGNORE)
          throw std::runtime_error("invalid status");
      } else if (mode == ActionMode::EXECUTE) {
        if (mask) {
          if (mask != 0x28a00000 || c.discrete_io_32() || running || armed ||
              c.timing_control() != TimestampControlMode::IGNORE)
            throw std::runtime_error("configuration must be complete and stopped");
          Settings requested{static_cast<double>(*c.rf_ref_frequency()),
                             static_cast<double>(*c.sample_rate()),
                             static_cast<double>(*c.bandwidth()), c.gain()->stage_1()};
          const auto supports = [](double value, const SoapySDR::RangeList& ranges) {
            return std::isfinite(value) &&
                   std::any_of(ranges.begin(), ranges.end(), [&](const auto& r) {
                     return value >= r.minimum() && value <= r.maximum();
                   });
          };
          error_fields = 0;
          if (!supports(requested.center, device.getFrequencyRange(SOAPY_SDR_RX, 0)))
            error_fields |= 1U << 27;
          if (!supports(requested.rate, device.getSampleRateRange(SOAPY_SDR_RX, 0)))
            error_fields |= 1U << 21;
          // Bandwidth validity is relative to the requested rate, not the old state.
          if (!std::isfinite(requested.bandwidth) || requested.bandwidth < 1 ||
              requested.bandwidth > requested.rate)
            error_fields |= 1U << 29;
          const auto gain_range = device.getGainRange(SOAPY_SDR_RX, 0);
          if (!supports(requested.gain, {gain_range})) error_fields |= 1U << 23;
          if (error_fields) {
            error.parameter_out_of_range(true);
            throw std::runtime_error("setting range");
          }
          if (requested.rate != std::floor(requested.rate)) {
            error_fields = 1U << 21;
            error.parameter_unsupported_precision(true);
            throw std::runtime_error("integral sample rate required");
          }
          if (c.gain()->stage_2() != 0) {
            error_fields = 1U << 23;
            error.field_value_invalid(true);
            throw std::runtime_error("unsupported gain stage");
          }
          error_fields = mask;
          device.configure(requested);
          configured = true;
        } else if (c.discrete_io_32() && c.discrete_io_32()->stream_enable_enable()) {
          if (c.discrete_io_32()->stream_enable()) {
            if (!configured || running || armed ||
                c.timing_control() != TimestampControlMode::DEVICE)
              throw std::runtime_error("invalid start state");
            const auto wall_ns =
                duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
            const auto target_ns = std::int64_t(c.integer_timestamp()) * 1000000000LL +
                                   static_cast<std::int64_t>(c.fractional_timestamp() / 1000);
            const auto lead = target_ns - wall_ns;
            if (lead < 20000000 || lead > 10000000000LL) {
              error.timestamp_problem(true);
              throw std::runtime_error("start deadline");
            }
            start = Clock::now() + nanoseconds(lead);
            epoch_sec = c.integer_timestamp();
            epoch_ps = c.fractional_timestamp();
            sample = 0;
            armed = true;
          } else {
            if (c.timing_control() != TimestampControlMode::IGNORE)
              throw std::runtime_error("timed stop unsupported");
            running = false;
            armed = false;
            device.deactivateStream(stream);
          }
        } else
          throw std::runtime_error("unsupported operation");
      } else
        throw std::runtime_error("unsupported action");
      if (c.req_x()) {
        RadioAckVX a;
        identity(a, c);
        a.ack_x(true);
        a.scheduled_or_executed(true);
        a.timing_control(c.timing_control());
        response = bytes(a);
      }
      if (c.req_s()) {
        const auto status_reply = [&](auto& a) {
          identity(a, c);
          const auto& s = device.settings();
          if (mask & (1U << 29)) a.bandwidth(s.bandwidth);
          if (mask & (1U << 27)) a.rf_ref_frequency(s.center);
          if (mask & (1U << 21)) a.sample_rate(s.rate);
          Gain gain;
          gain.stage_1(s.gain);
          if (mask & (1U << 23)) a.gain(gain);
          typename std::remove_cvref_t<decltype(a.discrete_io_32())>::value_type state;
          state.stream_enable_enable(true);
          state.stream_enable(running);
          if (word(request, 36) & 2U) a.discrete_io_32(state);
          auto status = bytes(a);
          response.insert(response.end(), status.begin(), status.end());
        };
        if (mode == ActionMode::NO_ACTION) {
          RadioQueryAckS a;
          status_reply(a);
        } else {
          RadioAckS a;
          status_reply(a);
        }
      }
    } catch (const std::exception&) {
      RadioAckVX a;
      identity(a, c);
      a.ack_x(true);
      if (error_fields & (1U << 29)) a.bandwidth_errors(error);
      if (error_fields & (1U << 27)) a.rf_ref_frequency_errors(error);
      if (error_fields & (1U << 23)) a.gain_errors(error);
      if (error_fields & (1U << 21)) a.sample_rate_errors(error);
      if (!error_fields) a.discrete_io_32_errors(error);
      if (error.timestamp_problem()) a.timing_control(TimestampControlMode::TIMING_ISSUES);
      response = bytes(a);
    }
    highest = c.message_id();
    replay.emplace(highest, std::pair{request, response});
    if (replay.size() > 256) replay.erase(replay.begin());
    return response;
  }
  template <class P>
  void timestamp(P& p) const {
    const auto rate = static_cast<std::uint64_t>(device.settings().rate);
    const auto ps = epoch_ps + (sample % rate) * 1000000000000ULL / rate;
    p.integer_timestamp(
        static_cast<std::uint32_t>(epoch_sec + sample / rate + ps / 1000000000000ULL));
    p.fractional_timestamp(ps % 1000000000000ULL);
  }
  void tick(int socket, const sockaddr_in& destination) {
    auto now = Clock::now();
    if (armed && now >= start) {
      armed = false;
      running = true;
      device.activateStream(stream);
    }
    if (!running) return;
    const auto rate = static_cast<std::uint64_t>(device.settings().rate);
    auto due = start + nanoseconds(static_cast<std::int64_t>((sample / rate) * 1000000000 +
                                                             (sample % rate) * 1000000000 / rate));
    if (now < due) return;
    // At most one packet per event-loop iteration; discard overdue whole samples
    // after 100 ms rather than accumulating an unbounded catch-up backlog.
    if (now - due > milliseconds(100)) {
      auto elapsed = duration_cast<nanoseconds>(now - start).count();
      auto expected = static_cast<std::uint64_t>(elapsed / 1000000000) * rate +
                      static_cast<std::uint64_t>(elapsed % 1000000000) * rate / 1000000000;
      device.skip_samples(expected - sample);
      dropped += expected - sample;
      sample = expected;
    }
    auto send = [&](auto& packet) {
      auto b = packet.data();
      auto result = sendto(socket, b.data(), b.size(), 0,
                           reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
      if (result != static_cast<ssize_t>(b.size())) ++udp_errors;
    };
    if (sample % burst == 0 || packets == 0) {
      RadioContext c;
      c.stream_id(id);
      timestamp(c);
      const auto& s = device.settings();
      c.bandwidth(s.bandwidth);
      c.rf_ref_frequency(s.center);
      c.sample_rate(s.rate);
      Gain gain;
      gain.stage_1(s.gain);
      c.gain(gain);
      send(c);
    }
    auto count = std::min<std::uint64_t>(1024, burst - sample % burst);
    std::array<std::int16_t, 2048> iq{};
    void* buffers[] = {iq.data()};
    int flags{};
    long long time{};
    if (device.readStream(stream, buffers, count, flags, time) != static_cast<int>(count))
      throw std::runtime_error("device read");
    Bytes payload(count * 4);
    for (size_t i = 0; i < count * 2; ++i) {
      auto v = static_cast<std::uint16_t>(iq[i]);
      payload[2 * i] = v >> 8;
      payload[2 * i + 1] = v & 255;
    }
    RadioData p;
    p.stream_id(id);
    p.packet_count(packets++ % 16);
    timestamp(p);
    p.payload(payload);
    bool first = sample % burst == 0, last = sample % burst + count == burst;
    p.trailer().sample_frame(first ? (last ? SSI::SINGLE : SSI::FIRST)
                                   : (last ? SSI::FINAL : SSI::MIDDLE));
    send(p);
    sample += count;
  }
};
}  // namespace
int run_radio(int argc, char** argv) {
  try {
    auto args = node_arguments(argc, argv);
    auto node = load_node_settings(args.config, args.node, "vita.radio");
    auto tcp = std::get<TcpTransportConfig>(
        std::get<ExternalTransportConfig>(node.port("control").edge.transport).protocol);
    auto udp = std::get<UdpTransportConfig>(
        std::get<ExternalTransportConfig>(node.port("samples").edge.transport).protocol);
    if (!tcp.tls.enabled || udp.max_datagram_bytes < 4128)
      throw std::runtime_error("radio needs mTLS and 4128-byte datagrams");
    const auto generation_text = config_internal::read_document(tcp.tls.generation_file, 8192);
    const auto generation = config_internal::parse_document(generation_text);
    if (generation.at("version") != ConfigValue(1) || generation.at("generation").integer() < 1)
      throw std::runtime_error("invalid TLS credential generation");
    for (const auto& path : {tcp.tls.ca_file, tcp.tls.certificate_file, tcp.tls.private_key_file}) {
      const auto name = std::filesystem::path(path).filename().string();
      if (config_internal::sha256(config_internal::read_document(path, 65536)) !=
          generation.at("members").at(name).text())
        throw std::runtime_error("incomplete TLS credential generation");
    }
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_server_method()),
                                                          SSL_CTX_free);
    if (!ctx || SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION) != 1 ||
        SSL_CTX_load_verify_locations(ctx.get(), tcp.tls.ca_file.c_str(), nullptr) != 1 ||
        SSL_CTX_use_certificate_chain_file(ctx.get(), tcp.tls.certificate_file.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx.get(), tcp.tls.private_key_file.c_str(),
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx.get()) != 1)
      throw std::runtime_error("TLS credentials");
    if (generation_text != config_internal::read_document(tcp.tls.generation_file, 8192))
      throw std::runtime_error("TLS credential generation changed during loading");
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    Fd listener(socket(AF_INET, SOCK_STREAM, 0)), sender(socket(AF_INET, SOCK_DGRAM, 0));
    auto local = address(tcp.bind, tcp.port), destination = address(udp.destination, udp.port);
    auto source = address(udp.bind, 1);
    source.sin_port = 0;
    if (bind(listener.value, reinterpret_cast<sockaddr*>(&local), sizeof(local)) ||
        listen(listener.value, 4) ||
        bind(sender.value, reinterpret_cast<sockaddr*>(&source), sizeof(source)))
      throw std::runtime_error("radio bind");
    fcntl(listener.value, F_SETFL, O_NONBLOCK);
    fcntl(sender.value, F_SETFL, O_NONBLOCK);
    auto& parameters = node.resolved.at("parameters");
    Radio radio(parameters.at("radio_index").integer(),
                parameters.at("signal_frequency_hz").integer(),
                parameters.at("burst_samples").integer(),
                parameters.at("amplitude_ppm").integer() / 1000000.0,
                parameters.at("initial_phase_mdeg").integer() * std::numbers::pi / 180000.0);
    stopping = 0;
    std::signal(SIGINT, stop_signal);
    std::signal(SIGTERM, stop_signal);
    std::signal(SIGPIPE, SIG_IGN);
    if (!await_node_release(node, args, [] { return stopping != 0; })) return 0;
    std::unique_ptr<UdpJsonTraceSink> trace;
    if (!node.resolved.at("telemetry").at("credential").is_null()) {
      auto secret = demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET");
      if (secret.empty()) throw std::runtime_error("missing staged telemetry credential");
      trace = std::make_unique<UdpJsonTraceSink>(
          node.id(), node.resolved.at("telemetry").at("host").text(),
          node.resolved.at("telemetry").at("port").integer(), secret,
          [] { return demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET"); });
    }
    std::unique_ptr<Client> client;
    auto log_at = Clock::now() + seconds(1);
    while (!stopping) {
      sockaddr_in peer{};
      socklen_t size = sizeof(peer);
      int socket = accept(listener.value, reinterpret_cast<sockaddr*>(&peer), &size);
      if (socket >= 0) {
        auto expected = address(node.port("control").source_address, 1);
        if (client || peer.sin_addr.s_addr != expected.sin_addr.s_addr)
          close(socket);
        else
          client = std::make_unique<Client>(socket, ctx.get());
      }
      if (client) {
        auto& c = *client;
        bool failed = Clock::now() > c.deadline;
        if (!failed && !c.ready) {
          int result = SSL_accept(c.ssl.get());
          if (result == 1) {
            auto* cert = SSL_get0_peer_certificate(c.ssl.get());
            c.ready = cert && X509_check_host(cert, "controller", 10,
                                              X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, nullptr) == 1;
            failed = !c.ready;
          } else {
            int error = SSL_get_error(c.ssl.get(), result);
            failed = error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE;
          }
        }
        if (!failed && c.ready) {
          if (c.output.empty()) {
            std::array<std::uint8_t, 1024> buffer{};
            int count = SSL_read(c.ssl.get(), buffer.data(), buffer.size());
            if (count > 0)
              c.input.insert(c.input.end(), buffer.begin(), buffer.begin() + count);
            else {
              auto error = SSL_get_error(c.ssl.get(), count);
              failed = error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE;
            }
            if (c.input.size() > 2048) failed = true;
            try {
              while (!failed && c.input.size() >= 4) {
                auto length = (word(c.input, 0) & 65535) * 4;
                if (length < 40 || length > 1024) throw std::runtime_error("frame length");
                if (c.input.size() < length) break;
                Bytes request(c.input.begin(), c.input.begin() + length);
                c.input.erase(c.input.begin(), c.input.begin() + length);
                auto response = radio.command(request);
                if (c.output.size() + response.size() > 4096)
                  throw std::runtime_error("reply bound");
                c.output.insert(c.output.end(), response.begin(), response.end());
                c.deadline = Clock::now() + seconds(2);
              }
            } catch (const std::exception&) {
              failed = true;
            }
          }
          if (!failed && !c.output.empty()) {
            int written = SSL_write(c.ssl.get(), c.output.data(), c.output.size());
            if (written > 0)
              c.output.erase(c.output.begin(), c.output.begin() + written);
            else {
              int error = SSL_get_error(c.ssl.get(), written);
              failed = error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE;
            }
          }
        }
        if (failed) client.reset();
      }
      radio.tick(sender.value, destination);
      if (Clock::now() >= log_at) {
        if (trace) trace->on_heartbeat(node.id(), 0);
        std::cout << "radio=" << radio.id << " packets=" << radio.packets
                  << " dropped_samples=" << radio.dropped << " udp_errors=" << radio.udp_errors
                  << " clipped=" << radio.device.clipped() << std::endl;
        log_at = Clock::now() + seconds(1);
      }
      poll(nullptr, 0, 0);
      // Sub-millisecond pacing without a busy spin; control stays serviced.
      std::this_thread::sleep_for(microseconds(100));
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "graphx-vita-radio: " << e.what() << '\n';
    return 1;
  }
}
}  // namespace graphx::vita
