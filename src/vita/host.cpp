#include "graphx/vita/runtime.hpp"
#include <SoapySDR/Constants.h>
#include <sys/socket.h>
#include <openssl/x509v3.h>
#include <cstring>
#include <stdexcept>

namespace graphx::vita {
using namespace std::chrono;
namespace rt = vr::runtime;
namespace tx = vr::runtime::transport;
namespace tr = vr::runtime::transaction;
HostTransport::HostTransport(int udp, sockaddr_in destination, bool controller)
    : udp_(udp), destination_(destination), controller_(controller) {}
HostTransport::Factory HostTransport::factory() {
  Factory result;
  result.context = this;
  result.required_bytes = sizeof(HostTransport) + 128;
  result.slot_capacity = slots_.size();
  result.capabilities.max_packet_bytes = 4128;
  result.capabilities.reserved_control_slots = 4;
  result.capabilities.reserved_cancellation_slots = 1;
  result.capabilities.per_stream_data_limit = 7;
  result.create = create;
  return result;
}
vr::Result<HostTransport::Binding> HostTransport::create(void* p, tx::HostBindings host) noexcept {
  auto& self = *static_cast<HostTransport*>(p);
  self.host_.emplace(host);
  self.ingress_.emplace(host.routes, host.rx_data, host.rx_control, host.rx_cancellation,
                        rt::PeerSession{self.controller_ ? 2u : 1u, 1}, 1024);
  Binding result;
  result.owner = self.shared_from_this();
  result.context = p;
  result.metadata_bytes = self.factory().required_bytes;
  result.slot_capacity = self.slots_.size();
  result.capabilities = self.factory().capabilities;
  result.send = [](void* p, Submission&& s) noexcept {
    return static_cast<HostTransport*>(p)->send(std::move(s));
  };
  result.progress = [](void* p) noexcept { return static_cast<HostTransport*>(p)->progress(); };
  result.pending = [](void* p, Token token) noexcept {
    auto& self = *static_cast<HostTransport*>(p);
    return token.slot < self.slots_.size() &&
           self.slots_[token.slot].generation == token.generation &&
           self.slots_[token.slot].submission.has_value();
  };
  result.close_admission = [](void* p) noexcept { static_cast<HostTransport*>(p)->closed_ = true; };
  result.quiescence = [](void* p, Token token) noexcept -> vr::Result<void> {
    auto& self = *static_cast<HostTransport*>(p);
    if (token.slot < self.slots_.size() && self.slots_[token.slot].generation == token.generation &&
        self.slots_[token.slot].submission)
      return std::unexpected(vr::Error{vr::ErrorCode::invalid_state});
    return {};
  };
  result.associate = [](void*, std::span<const tx::Association> batch,
                        bool) noexcept -> vr::Result<void> {
    for (auto& a : batch)
      if (a.sid < 1 || a.sid > 4 || a.local.generation != 1 || a.remote.generation != 1)
        return std::unexpected(vr::Error{vr::ErrorCode::identity_conflict});
    return {};
  };
  result.detach = [](void* p) noexcept { static_cast<HostTransport*>(p)->detach(); };
  return result;
}
std::expected<HostTransport::Token, tx::RejectedSubmission> HostTransport::send(
    Submission&& s) noexcept {
  auto reject = [&](vr::ErrorCode code) -> std::expected<Token, tx::RejectedSubmission> {
    return std::unexpected(tx::RejectedSubmission{vr::Error{code}, std::move(s)});
  };
  if (closed_ || !host_) return reject(vr::ErrorCode::invalid_state);
  const bool control = vr::codec::is_command(s.counter.type);
  auto size = s.storage.byte_size();
  if (!size || size > (control ? 1024u : 4128u)) return reject(vr::ErrorCode::resource_limit);
  if (control && queued_control_ + size > 4096) return reject(vr::ErrorCode::capacity_exhausted);
  if (!s.completion.is_reserved() || !s.source.generation || s.storage.segment_count() > 3)
    return reject(vr::ErrorCode::invalid_argument);
  std::size_t offset = 0;
  for (std::size_t j = 0; j < s.storage.segment_count(); ++j) {
    auto part = s.storage.segment(j);
    if (!part || part->size() > size - offset) return reject(vr::ErrorCode::invalid_argument);
    std::memcpy(staging_.data() + offset, part->data(), part->size());
    offset += part->size();
  }
  if (offset != size) return reject(vr::ErrorCode::invalid_argument);
  auto envelope = vr::codec::decode_envelope(vr::Bytes{staging_}.first(size));
  if (!envelope || envelope->envelope.stream_id != s.counter.stream_id ||
      envelope->envelope.type != s.counter.type)
    return reject(vr::ErrorCode::invalid_argument);
  const bool cancel = envelope->envelope.cancel;
  // Reserve a cancellation slot and its byte budget independently of normal replies.
  if (control && !cancel && queued_control_ + size > 3072)
    return reject(vr::ErrorCode::capacity_exhausted);
  const auto first = control ? (cancel ? 11u : 7u) : 0u;
  const auto end = control ? (cancel ? 12u : 11u) : 7u;
  for (std::size_t i = first; i < end; ++i) {
    auto& slot = slots_[i];
    if (slot.submission) continue;
    if (sequence_ == UINT64_MAX || slot.generation == UINT64_MAX)
      return reject(vr::ErrorCode::overflow);
    std::memcpy(slot.bytes.data(), staging_.data(), size);
    auto count = host_->counters.accept(s.counter, envelope->envelope.packet_count);
    if (!count) return reject(count.error().code);
    slot.size = size;
    slot.offset = 0;
    slot.control = control;
    slot.sequence = ++sequence_;
    slot.deadline = steady_clock::now() + seconds(2);
    slot.submission.emplace(std::move(s));
    if (control) queued_control_ += size;
    return Token{i, slot.generation};
  }
  return reject(vr::ErrorCode::capacity_exhausted);
}
void HostTransport::finish(Slot& slot, bool success) noexcept {
  rt::CompletionResult result;
  result.value = slot.size;
  if (!success) {
    result.status = rt::CompletionStatus::failed;
    result.error = vr::Error{vr::ErrorCode::callback_failure};
  }
  slot.submission->completion.publish(result);
  if (slot.control) queued_control_ -= slot.size;
  slot.submission.reset();  // releases storage and admission credit only after completion
  ++slot.generation;
}
vr::Result<bool> HostTransport::progress() noexcept {
  Slot* next = nullptr;
  for (auto& slot : slots_)
    if (slot.submission && (!next || (next->control && !slot.control) ||
                            (slot.control == next->control && slot.sequence < next->sequence)))
      next = &slot;
  if (!next) return false;
  auto& slot = *next;
  if (!slot.control) {
    auto count = sendto(udp_, slot.bytes.data(), slot.size, 0,
                        reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
    bool ok = count == static_cast<ssize_t>(slot.size);
    if (!ok) ++udp_errors;
    finish(slot, ok);
    return true;
  }
  if (!ssl_ || failed_ || steady_clock::now() >= slot.deadline) {
    failed_ = ssl_ != nullptr;
    finish(slot, false);
    return true;
  }
  int count =
      SSL_write(ssl_, slot.bytes.data() + slot.offset, static_cast<int>(slot.size - slot.offset));
  if (count > 0) {
    slot.offset += static_cast<std::size_t>(count);
    if (slot.offset == slot.size) finish(slot, true);
    return true;
  }
  int error = SSL_get_error(ssl_, count);
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return false;
  failed_ = true;
  finish(slot, false);
  return true;
}
void HostTransport::connect(SSL* ssl) {
  if (!ssl || !ingress_ || closed_ || ssl_)
    throw std::runtime_error("invalid authenticated TLS binding");
  const char* identity = controller_ ? "radio" : "controller";
  auto* certificate = SSL_get0_peer_certificate(ssl);
  if (!SSL_is_init_finished(ssl) || !(SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER) ||
      SSL_get_verify_result(ssl) != X509_V_OK || !certificate ||
      X509_check_host(certificate, identity, std::strlen(identity),
                      X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, nullptr) != 1)
    throw std::runtime_error("unauthenticated VITA TLS peer");
  ssl_ = ssl;
  failed_ = false;
  ingress_->reconnect({controller_ ? 2u : 1u, 1});
  input_deadline_ = steady_clock::now() + seconds(2);
}
void HostTransport::disconnect() {
  ssl_ = nullptr;
  if (ingress_) ingress_->disconnect();
  for (auto& slot : slots_)
    if (slot.submission && slot.control) finish(slot, false);
}
bool HostTransport::receive() {
  if (!ssl_ || failed_ || steady_clock::now() >= input_deadline_) return false;
  int count = SSL_read(ssl_, input_.data(), input_.size());
  if (count > 0) {
    bool stalled = ingress_->stalled();
    auto accepted = ingress_->feed(vr::Bytes{input_}.first(static_cast<std::size_t>(count)));
    if (!accepted) return false;
    // A peer cannot extend a partial-frame deadline by dribbling bytes.
    if (!stalled || !ingress_->stalled()) input_deadline_ = steady_clock::now() + seconds(2);
    return true;
  }
  auto error = SSL_get_error(ssl_, count);
  return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
}
void HostTransport::detach() noexcept {
  disconnect();
  for (auto& slot : slots_)
    if (slot.submission) finish(slot, false);
  ingress_.reset();
  host_.reset();
  closed_ = true;
}
DeviceBinding::DeviceBinding(double signal, double amplitude, double phase)
    : device(signal, amplitude, phase) {
  stream_ = device.setupStream(SOAPY_SDR_RX, "CS16");
}
DeviceBinding::~DeviceBinding() { device.closeStream(stream_); }
vr::profiles::iq::GraphxCapabilities DeviceBinding::capabilities() const {
  vr::profiles::iq::GraphxCapabilities result;
  const auto convert = [](const SoapySDR::Range& range) {
    return vr::profiles::iq::CapabilityRange<vr::Hertz>{
        *vr::Hertz::from_integer(static_cast<std::int64_t>(range.minimum())),
        *vr::Hertz::from_integer(static_cast<std::int64_t>(range.maximum()))};
  };
  result.center_frequency = convert(device.getFrequencyRange(SOAPY_SDR_RX, 0).front());
  result.sample_rate = convert(device.getSampleRateRange(SOAPY_SDR_RX, 0).front());
  result.bandwidth = convert(device.getBandwidthRange(SOAPY_SDR_RX, 0).front());
  const auto gain = device.getGainRange(SOAPY_SDR_RX, 0);
  result.gain = {{static_cast<std::int16_t>(gain.minimum() * 128), 0},
                 {static_cast<std::int16_t>(gain.maximum() * 128), 0}};
  return result;
}
namespace {
std::optional<Settings> batch_settings(const rt::ExecutionPlan& plan) noexcept {
  if (plan.count != 4) return {};
  Settings result;
  unsigned seen = 0;
  for (std::size_t i = 0; i < plan.count; ++i) {
    const auto& field = plan.fields[i];
    unsigned bit = 0;
    if (field.id == vr::Gain::id) {
      const auto* gain = std::get_if<vr::GainStages>(&field.adjusted);
      if (!gain || gain->stage2_q7) return {};
      result.gain = gain->stage1_q7 / 128.0;
      bit = 8;
    } else {
      const auto* hz = std::get_if<vr::Hertz>(&field.adjusted);
      if (!hz) return {};
      const double value = hz->q20 / 1048576.0;
      if (field.id == vr::Bandwidth::id) {
        result.bandwidth = value;
        bit = 1;
      }
      if (field.id == vr::RFReferenceFrequency::id) {
        result.center = value;
        bit = 2;
      }
      if (field.id == vr::SampleRate::id) {
        result.rate = value;
        bit = 4;
      }
    }
    if (!bit || (seen & bit)) return {};
    seen |= bit;
  }
  try {
    VirtualDevice::validate(result);
  } catch (...) {
    return {};
  }
  return result;
}
}  // namespace
vr::DeviceBackendBinding DeviceBinding::binding() {
  vr::DeviceBackendBinding result;
  result.owner = shared_from_this();
  result.storage_bytes = sizeof(DeviceBinding) + 128;
  result.backend.context = this;
  result.backend.validate = [](void*, vr::FieldId, vr::SemanticValue value,
                               const rt::StateSnapshot&) noexcept {
    return tr::Validation{
        value, {}, true};  // profile validates ranges and cross-setting constraints
  };
  result.backend.validate_plan = [](void*, const rt::ExecutionPlan& plan,
                                    const rt::StateSnapshot&) noexcept -> vr::Result<void> {
    if (plan.count == 1 && plan.fields[0].id == vr::DiscreteIO32::id) return {};
    if (!batch_settings(plan)) return std::unexpected(vr::Error{vr::ErrorCode::invalid_argument});
    return {};
  };
  result.backend.begin = begin;
  result.backend.commit = commit;
  result.backend.quiescence = [](void*) noexcept { return tr::BackendQuiescence{true, true, 0}; };
  result.progress = [](void* p, const tr::OperationContext& now) noexcept {
    static_cast<DeviceBinding*>(p)->now_ = now.clock.time;
  };
  return result;
}
tr::BatchOutcome DeviceBinding::commit(void* p, const rt::ExecutionPlan& plan,
                                       rt::timing::Boundary) noexcept {
  auto& self = *static_cast<DeviceBinding*>(p);
  auto settings = batch_settings(plan);
  if (!settings) return {rt::FieldStatus::failed, self.actual_time(), true};
  try {
    // VirtualDevice validates before its single, nonthrowing Settings assignment.
    self.device.configure(*settings);
    return {rt::FieldStatus::executed, self.actual_time(), true};
  } catch (...) {
    return {rt::FieldStatus::failed, self.actual_time(), true};
  }
}
vr::Result<void> DeviceBinding::begin(void* p, const rt::PlannedField& field,
                                      rt::timing::Boundary boundary,
                                      tr::AsyncResult completion) noexcept {
  auto& self = *static_cast<DeviceBinding*>(p);
  rt::FieldOutcome outcome;
  outcome.id = field.id;
  outcome.value = field.adjusted;
  outcome.validity = rt::Validity::known;
  outcome.actual_time = self.actual_time();
  outcome.time_known = true;
  outcome.sample_ordinal = boundary.sample_ordinal;
  outcome.ordinal_known = true;
  try {
    if (field.id != vr::DiscreteIO32::id)
      return std::unexpected(vr::Error{vr::ErrorCode::unsupported_capability});
    const bool running = std::get<std::uint32_t>(field.adjusted) == 3;
    const int status = running ? self.device.activateStream(self.stream_)
                               : self.device.deactivateStream(self.stream_);
    outcome.status = status == 0 ? rt::FieldStatus::executed : rt::FieldStatus::failed;
    outcome.actual_time = self.actual_time();
  } catch (...) {
    outcome.status = rt::FieldStatus::unknown_effect;
    outcome.validity = rt::Validity::unknown;
    outcome.time_known = false;
  }
  if (!completion.complete(outcome))
    return std::unexpected(vr::Error{vr::ErrorCode::invalid_state});
  return {};
}
vr::profiles::iq::SourceProvider DeviceBinding::source() {
  return {this, samples, [](void* p, const rt::EffectiveEvent& event) noexcept -> vr::Result<void> {
            auto& self = *static_cast<DeviceBinding*>(p);
            if (event.sample_epoch) {
              self.sample_epoch_ = event.sample_epoch;
              self.ordinal_ = 0;
              self.device.begin_epoch();
            }
            return {};
          }};
}
vr::Result<void> DeviceBinding::samples(void* p,
                                        vr::profiles::iq::SampleWriteWindow& window) noexcept {
  auto& self = *static_cast<DeviceBinding*>(p);
  try {
    if (window.first_ordinal() < self.ordinal_)
      return std::unexpected(vr::Error{vr::ErrorCode::invalid_state});
    self.device.skip_samples(window.first_ordinal() - self.ordinal_);
    std::array<std::int16_t, 2048> iq{};
    void* buffers[]{iq.data()};
    int flags{};
    long long time{};
    if (self.device.readStream(self.stream_, buffers, window.count(), flags, time, 0) !=
        static_cast<int>(window.count()))
      return std::unexpected(vr::Error{vr::ErrorCode::callback_failure});
    self.ordinal_ = window.first_ordinal() + window.count();
    for (std::size_t i = 0; i < window.count(); ++i) {
      auto written = window.write_iq16(i, iq[2 * i], iq[2 * i + 1]);
      if (!written) return written;
    }
    return {};
  } catch (...) {
    return std::unexpected(vr::Error{vr::ErrorCode::callback_failure});
  }
}
HostClock::HostClock() : start_(steady_clock::now()) {
  auto ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
  epoch_ = {static_cast<std::uint64_t>(ns / 1000000000),
            static_cast<std::uint64_t>(ns % 1000000000) * 1000};
}
vr::runtime::timing::ProtocolTime HostClock::now() const noexcept {
  auto elapsed =
      static_cast<std::uint64_t>(duration_cast<nanoseconds>(steady_clock::now() - start_).count());
  return *rt::timing::add(epoch_, rt::timing::from_nanoseconds(elapsed));
}
void HostClock::progress(Runtime& runtime) {
  auto ns =
      static_cast<std::uint64_t>(duration_cast<nanoseconds>(steady_clock::now() - start_).count());
  if (!mapped_) {
    auto time = rt::timing::add(epoch_, rt::timing::from_nanoseconds(ns));
    if (!time || !runtime.observe_pps({ns}, *time)) throw std::runtime_error("VITA clock binding");
    mapped_ = true;
  }
  auto result = runtime.progress({ns});
  // A bounded transport/pool can reject admission. The runtime retains the
  // unsent response; service completions and retry on the next host turn.
  if (!result && result.error().code == vr::ErrorCode::capacity_exhausted) return;
  if (!result)
    throw std::runtime_error("VITA progress " +
                             std::to_string(static_cast<int>(result.error().code)));
}
ControllerSession::ControllerSession(std::uint32_t radio_id)
    : transport_(std::make_shared<HostTransport>(-1, sockaddr_in{}, true)) {
  auto config = runtime_config();
  config.transport = transport_->factory();
  auto made = Runtime::create(config, runtime_pools());
  if (!made) throw std::runtime_error("VITA controller runtime setup");
  runtime_ = std::move(*made);
  vr::RemoteTargetConfig target;
  target.sid = target.controllee_id = radio_id;
  target.controller_id = 1;
  target.profile = vr::profiles::iq::Profile::graphx_radio;
  auto added = runtime_->add_remote_controller(target);
  if (!added) throw std::runtime_error("VITA controller identity");
  controller_ = *added;
  clock_.progress(*runtime_);
}
ControllerSession::~ControllerSession() {
  disconnect();
  runtime_->shutdown(vr::StopMode::immediate);
}
void ControllerSession::connect(SSL* peer) {
  transport_->connect(peer);
  connected_ = true;
}
void ControllerSession::disconnect() {
  transport_->disconnect();
  connected_ = false;
}
bool ControllerSession::progress() {
  clock_.progress(*runtime_);
  if (!connected_) return false;
  if (!transport_->healthy() || !transport_->receive()) {
    disconnect();
    return false;
  }
  return true;
}
vr::RuntimeConfig runtime_config() {
  auto result = *vr::profiles::iq::lab::config(vr::profiles::iq::graphx_unknown_oui);
  result.clock.epoch = rt::timing::Epoch::utc;
  // Simulated UTC has one immutable monotonic mapping, not a GPS/PPS source.
  // Holdover is explicit and unbounded for this deterministic software clock.
  result.clock.holdover_limit_ns = UINT64_MAX;
  result.timing.device_early_ps = 0;
  result.timing.device_late_ps = 100000000000;  // declared 100 ms host activation tolerance
  return result;
}
vr::ExternalPools runtime_pools() {
  vr::profiles::iq::lab::PoolCounts counts;
  counts.payload_bytes = 4096;
  counts.rx_data_bytes = 4160;
  auto pools = vr::profiles::iq::lab::pools(counts);
  if (!pools) throw std::runtime_error("VITA pools");
  return std::move(*pools);
}
}  // namespace graphx::vita
