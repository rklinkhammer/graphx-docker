#pragma once
#include "graphx/vita/virtual_device.hpp"
#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>
#include <vita/runtime/transport/stream_framer.hpp>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <chrono>

namespace graphx::vita {
namespace vr = ::vita;
using Runtime = vr::VitaRuntime<1, 16, 256, 2 * 1024 * 1024>;
// All callbacks and TLS operations run in the radio's serialized domain.
class HostTransport : public std::enable_shared_from_this<HostTransport> {
 public:
  using Binding = vr::runtime::transport::TransportBinding;
  using Factory = vr::runtime::transport::TransportFactory;
  using Submission = vr::runtime::transport::TxSubmission;
  using Token = vr::runtime::transport::TxToken;
  using Ingress = vr::runtime::transport::StreamIngress<1024, 128>;
  HostTransport(int udp, sockaddr_in destination, bool controller = false);
  Factory factory();
  void connect(SSL* ssl);
  void disconnect();
  bool receive();
  bool datagram(vr::Bytes);
  bool healthy() const { return !failed_; }
  std::uint64_t udp_errors{}, udp_sent{}, udp_sent_bytes{};

 private:
  struct Slot {
    std::optional<Submission> submission;
    std::array<std::byte, 4128> bytes{};
    std::size_t size{}, offset{};
    std::uint64_t generation{1}, sequence{};
    bool control{};
    std::chrono::steady_clock::time_point deadline;
  };
  std::array<Slot, 12> slots_{};
  std::array<std::byte, 4128> staging_{};
  std::array<std::byte, 2048> input_{};
  std::optional<vr::runtime::transport::HostBindings> host_;
  std::optional<Ingress> ingress_;
  std::optional<vr::runtime::transport::StreamIngress<4128, 128>> datagrams_;
  int udp_;
  sockaddr_in destination_;
  SSL* ssl_{};
  bool controller_{}, closed_{}, failed_{};
  std::uint64_t sequence_{};
  std::size_t queued_control_{};
  std::chrono::steady_clock::time_point input_deadline_;
  static vr::Result<Binding> create(void*, vr::runtime::transport::HostBindings) noexcept;
  std::expected<Token, vr::runtime::transport::RejectedSubmission> send(Submission&&) noexcept;
  vr::Result<bool> progress() noexcept;
  void finish(Slot&, bool) noexcept;
  void detach() noexcept;
};

class DeviceBinding : public std::enable_shared_from_this<DeviceBinding> {
 public:
  VirtualDevice device;
  DeviceBinding(double signal, double amplitude, double phase);
  ~DeviceBinding();
  vr::DeviceBackendBinding binding();
  vr::profiles::iq::SourceProvider source();
  void set_clock(void* context, vr::runtime::timing::ProtocolTime (*read)(void*) noexcept) {
    clock_context_ = context;
    clock_read_ = read;
  }
  const auto& sample_epoch() const { return sample_epoch_; }
  vr::profiles::iq::GraphxCapabilities capabilities() const;

 private:
  void* clock_context_{};
  vr::runtime::timing::ProtocolTime (*clock_read_)(void*) noexcept {};
  std::optional<vr::runtime::timing::ProtocolTime> sample_epoch_;
  vr::runtime::timing::ProtocolTime actual_time() const noexcept {
    return clock_read_ ? clock_read_(clock_context_) : now_;
  }
  SoapySDR::Stream* stream_;
  std::uint64_t ordinal_{};
  vr::runtime::timing::ProtocolTime now_{};
  static vr::runtime::transaction::BatchOutcome commit(void*, const vr::runtime::ExecutionPlan&,
                                                       vr::runtime::timing::Boundary) noexcept;
  static vr::Result<void> begin(void*, const vr::runtime::PlannedField&,
                                vr::runtime::timing::Boundary,
                                vr::runtime::transaction::AsyncResult) noexcept;
  static vr::Result<void> samples(void*, vr::profiles::iq::SampleWriteWindow&) noexcept;
};

// One immutable UTC/steady mapping per process. Explicit simulated holdover
// never follows subsequent wall-clock adjustments. No GPS claim.
class HostClock {
 public:
  HostClock();
  void progress(Runtime&);
  vr::runtime::timing::ProtocolTime now() const noexcept;

 private:
  std::chrono::steady_clock::time_point start_;
  vr::runtime::timing::ProtocolTime epoch_;
  bool mapped_{};
};
// One authenticated radio relationship. Reconnect preserves the runtime and IDs.
// Call commands() for the library's configure/start/stop/status/capability/cancel
// APIs; progress() drives real host time (do not use synthetic run_for/wait budgets).
class ControllerSession {
 public:
  explicit ControllerSession(std::uint32_t radio_id,
                             vr::runtime::context::ReceiverBinding receiver = {});
  ~ControllerSession();
  Runtime::Controller& commands() { return *controller_; }
  void connect(SSL* authenticated_peer);
  void disconnect();
  bool progress();
  bool datagram(vr::Bytes wire) { return transport_->datagram(wire); }
  vr::runtime::timing::ProtocolTime now() const noexcept { return clock_.now(); }

 private:
  HostClock clock_;
  std::shared_ptr<HostTransport> transport_;
  std::unique_ptr<Runtime> runtime_;
  std::optional<Runtime::Controller> controller_;
  bool connected_{};
};
vr::RuntimeConfig runtime_config();
vr::ExternalPools runtime_pools();
}  // namespace graphx::vita
