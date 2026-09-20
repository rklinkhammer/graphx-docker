// Standalone regression for vrt_framework 60a290c9: compile/run commands and
// expected failing result are in design/four-radio-vita/migration-blocker.md.
// This intentionally stays outside the current vrtgen application's CTest suite.
#include <vita/profiles/iq/lab.hpp>
#include <vita/runtime/public/runtime.hpp>
#include <cstdlib>
#include <string_view>
#include <iostream>
using namespace vita;
namespace rt = vita::runtime;
namespace tr = vita::runtime::transaction;
template <class T>
void require(const T& result) {
  if (!result) {
    std::cerr << "setup/progress failure\n";
    std::exit(2);
  }
}
struct Device {
  rt::timing::ProtocolTime now{};
  rt::timing::ProtocolTime actual{};
  DeviceBackendBinding binding(const std::shared_ptr<Device>& owner) {
    DeviceBackendBinding b;
    b.owner = owner;
    b.storage_bytes = sizeof(Device) + 128;
    b.backend.context = this;
    b.backend.validate = [](void*, FieldId, SemanticValue value,
                            const rt::StateSnapshot&) noexcept {
      return tr::Validation{value, {}, true};
    };
    b.backend.commit = [](void* p, const rt::ExecutionPlan&, rt::timing::Boundary) noexcept {
      return tr::BatchOutcome{rt::FieldStatus::executed, static_cast<Device*>(p)->now, true};
    };
    b.backend.begin = [](void* p, const rt::PlannedField& field, rt::timing::Boundary,
                         tr::AsyncResult completion) noexcept -> Result<void> {
      auto& self = *static_cast<Device*>(p);
      self.actual = self.now;
      rt::FieldOutcome outcome;
      outcome.id = field.id;
      outcome.value = field.adjusted;
      outcome.status = rt::FieldStatus::executed;
      outcome.validity = rt::Validity::known;
      outcome.actual_time = self.actual;
      outcome.time_known = true;
      if (!completion.complete(outcome)) return std::unexpected(Error{ErrorCode::invalid_state});
      return {};
    };
    b.backend.quiescence = [](void*) noexcept { return tr::BackendQuiescence{true, true, 0}; };
    b.progress = [](void* p, const tr::OperationContext& context) noexcept {
      static_cast<Device*>(p)->now = context.clock.time;
    };
    return b;
  }
};
struct Capture {
  std::optional<rt::timing::ProtocolTime> first;
  static void receive(void* p, const rt::context::BorrowedSignalRx& rx) noexcept {
    auto& self = *static_cast<Capture*>(p);
    if (!self.first) self.first = rx.sample_time;
  }
};
int main(int argc, char** argv) {
  const bool on_time = argc == 2 && std::string_view(argv[1]) == "--on-time";
  if (argc > 2 || (argc == 2 && !on_time)) return 2;
  auto config = profiles::iq::lab::config(profiles::iq::graphx_unknown_oui);
  require(config);
  config->clock.epoch = rt::timing::Epoch::utc;
  config->timing.device_early_ps = 0;
  config->timing.device_late_ps = 100'000'000'000;  // GraphX's 100 ms host tolerance
  profiles::iq::lab::PoolCounts counts;
  counts.payload_bytes = 4096;
  counts.rx_data_bytes = 4160;
  auto pools = profiles::iq::lab::pools(counts);
  require(pools);
  auto made = VitaRuntime<1, 4, 32, 65536>::create(*config, std::move(*pools));
  require(made);
  auto& runtime = **made;
  auto device = std::make_shared<Device>();
  Capture capture;
  StreamConfig stream;
  stream.sid = stream.controllee_id = stream.controller_id = 1;
  stream.profile = profiles::iq::Profile::graphx_radio;
  stream.trailer = true;
  stream.ip_mtu = 9000;
  stream.maximum_samples_per_packet = 1024;
  stream.bandwidth = 800000;
  stream.device = device->binding(device);
  stream.receiver = {&capture, Capture::receive};
  auto radio = runtime.add_controllee(stream);
  require(radio);
  auto controller = runtime.add_controller(*radio);
  require(controller);
  require(runtime.observe_pps({0}, {1000, 0}));
  GraphxRadioSettings settings;
  settings.bandwidth = *Hertz::from_integer(800000);
  auto configuration = controller->configure(settings);
  require(configuration);
  require(runtime.run_for(5'000'000));
  auto configured = controller->wait(*configuration, 0, WaitEvidence::execution);
  require(configured);
  if (!configured->observation.confirms_execution) return 2;
  const rt::timing::ProtocolTime scheduled{1000, 50'000'000'000};
  auto start = controller->start(scheduled);
  require(start);
  require(runtime.run_for(44'000'000));  // admit and arm before deadline
  if (capture.first) return 2;
  require(runtime.progress(
      {on_time ? 50'000'000u : 50'500'000u}));  // device executes 0.5 ms late, within tolerance
  require(runtime.run_for(2'000'000));
  auto executed = controller->wait(*start, 0, WaitEvidence::execution);
  require(executed);
  if (!executed->observation.confirms_execution || !capture.first) return 2;
  std::cout << "scheduled=" << scheduled.seconds << ':' << scheduled.picoseconds
            << " actual=" << device->actual.seconds << ':' << device->actual.picoseconds
            << " first_sample=" << capture.first->seconds << ':' << capture.first->picoseconds
            << '\n';
  // Actual execution must be honest AND the simulated sample epoch must stay scheduled.
  if (*capture.first != scheduled) {
    std::cerr << "FAIL: host scheduling jitter changed the simulated sample epoch\n";
    return 1;
  }
  return 0;
}
