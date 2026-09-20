#include "graphx/vita/runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace graphx::vita;
namespace rt = vr::runtime;
namespace tr = rt::transaction;
void require(bool result) {
  if (!result) throw std::runtime_error("Soapy/runtime integration assertion");
}
struct Capture {
  std::size_t packets{};
  static void receive(void* p, const rt::context::BorrowedSignalRx&) noexcept {
    ++static_cast<Capture*>(p)->packets;
  }
};
// Device failure injection is test-owned; production has no fault-control API.
tr::BatchOutcome (*commit_device)(void*, const rt::ExecutionPlan&, rt::timing::Boundary) noexcept;
rt::FieldStatus fault = rt::FieldStatus::executed;
tr::BatchOutcome injected_commit(void* p, const rt::ExecutionPlan& plan,
                                 rt::timing::Boundary boundary) noexcept {
  if (fault == rt::FieldStatus::failed) return {fault, boundary.time, true};
  auto result = commit_device(p, plan, boundary);
  if (fault == rt::FieldStatus::unknown_effect) result.status = fault;
  return result;
}
int main() try {
  auto device = std::make_shared<DeviceBinding>(100050000, .25, 0);
  auto binding = device->binding();
  // Directly exercise defensive all-before-mutation validation at the host boundary.
  rt::ExecutionPlan incomplete;
  auto before = device->device.settings();
  auto denied = binding.backend.commit(binding.backend.context, incomplete, {});
  require(denied.status == rt::FieldStatus::failed &&
          device->device.settings().center == before.center);
  commit_device = binding.backend.commit;
  binding.backend.commit = injected_commit;
  auto made = Runtime::create(runtime_config(), runtime_pools());
  require(bool(made));
  auto& runtime = **made;
  Capture capture;
  vr::StreamConfig config;
  config.sid = config.controller_id = config.controllee_id = 1;
  config.profile = vr::profiles::iq::Profile::graphx_radio;
  config.ip_mtu = 9000;
  config.maximum_samples_per_packet = 1024;
  config.burst_pairs = 2050;
  config.trailer = true;
  config.bandwidth = 800000;
  config.device = binding;
  config.source = device->source();
  config.graphx_capabilities = device->capabilities();
  config.receiver = {&capture, Capture::receive};
  auto radio = runtime.add_controllee(config);
  require(bool(radio));
  auto controller = runtime.add_controller(*radio);
  require(bool(controller));
  require(bool(runtime.observe_pps({0}, {1000, 0})));
  vr::GraphxRadioSettings settings;
  settings.bandwidth = *vr::Hertz::from_integer(800000);
  settings.sample_rate = *vr::Hertz::from_integer(1000003);
  auto configure = controller->configure(settings);
  require(bool(configure));
  require(bool(runtime.run_for(1000000)));
  require(controller->wait(*configure, 0)->observation.confirms_execution);
  require(device->device.settings().rate == 1000003 && capture.packets == 0);
  auto invalid = settings;
  invalid.bandwidth = *vr::Hertz::from_integer(2000000);
  auto rejected = controller->configure(invalid);
  require(bool(rejected));
  require(bool(runtime.run_for(1000000)));
  require(!controller->wait(*rejected, 0)->observation.confirms_execution);
  require(device->device.settings().bandwidth == 800000);
  fault = rt::FieldStatus::failed;
  settings.center_frequency = *vr::Hertz::from_integer(100010000);
  auto failure = controller->configure(settings);
  require(bool(failure));
  require(bool(runtime.run_for(1000000)));
  require(!controller->wait(*failure, 0)->observation.confirms_execution);
  require(device->device.settings().center == 100000000);
  fault = rt::FieldStatus::executed;
  auto start = controller->start({1000, 50000000000});
  require(bool(start));
  require(bool(runtime.run_for(46000000)));
  require(bool(runtime.progress({50500000})));
  require(bool(runtime.run_for(3000000)));
  require(controller->wait(*start, 0)->observation.confirms_execution && capture.packets > 0);
  require(device->sample_epoch() == rt::timing::ProtocolTime{1000, 50000000000});
  auto stop = controller->stop();
  require(bool(stop));
  require(bool(runtime.run_for(1000000)));
  require(controller->wait(*stop, 0)->observation.confirms_execution);
  auto received = capture.packets;
  // An effect may have happened, so the library must fault instead of promising rollback.
  fault = rt::FieldStatus::unknown_effect;
  auto unknown = controller->configure(settings);
  require(bool(unknown));
  require(bool(runtime.run_for(1000000)));
  require(!controller->wait(*unknown, 0)->observation.confirms_execution);
  require(radio->status() == vr::SourceStatus::faulted);
  require(device->device.settings().center == 100010000);
  auto state = radio->confirmed_state();
  for (auto index : {1u, 4u, 5u, 6u})
    require(state.fields[index].validity == rt::Validity::unknown);
  require(bool(runtime.run_for(100000000)));
  require(capture.packets == received);
  std::cout << "Soapy binding: atomic rejection, known failure, unknown effects and scheduled "
               "source epoch passed\n";
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
