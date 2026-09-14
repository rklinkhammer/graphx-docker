#include "infra/fault_resources.hpp"

#include "infra/capture_resources.hpp"
#include "infra/command_runner.hpp"
#include "infra/endpoint_resources.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace graphx::infra::detail {
namespace {
int run(const std::vector<std::string>& arguments, std::string* captured = nullptr) {
  CommandOptions options;
  options.arguments = arguments;
  options.capture_output = captured != nullptr;
  auto result = run_command(options);
  if (captured) *captured = std::move(result.output);
  if (result.output_truncated) throw std::runtime_error("command output exceeded safety limit");
  return result.status;
}
}  // namespace

std::string boot_identity() {
  std::ifstream input("/proc/sys/kernel/random/boot_id");
  std::string value;
  std::getline(input, value);
  if (value.size() != 36 || !std::ranges::all_of(value, [](unsigned char character) {
        return std::isxdigit(character) || character == '-';
      }))
    return {};
  return value;
}

std::uint64_t monotonic_nanoseconds() {
  struct timespec value{};
  if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0)
    throw std::system_error(errno == 0 ? EINVAL : errno, std::generic_category(),
                            "cannot read monotonic clock");
  return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
         static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint32_t spawn_fault_timer(
    std::uint32_t duration, const std::string& interface, std::uint32_t expected_ifindex,
    const std::string& expected_qdisc,
    const std::function<void(std::uint32_t, const std::string&)>& registered) {
  int gate[2]{};
  if (::pipe(gate) != 0) throw std::system_error(errno, std::generic_category());
  const auto child = ::fork();
  if (child < 0) {
    ::close(gate[0]);
    ::close(gate[1]);
    throw std::system_error(errno, std::generic_category());
  }
  if (child == 0) {
    ::close(gate[1]);
#if defined(__linux__)
    ::prctl(PR_SET_NAME, "graphx-fault", 0, 0, 0);
#endif
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    char release{};
    const auto count = ::read(gate[0], &release, 1);
    ::close(gate[0]);
    if (count != 1 || release != '1') ::_exit(1);
    detach_child_io();
    struct timespec remaining{static_cast<time_t>(duration), 0};
    while (::nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
    std::ifstream identity("/sys/class/net/" + interface + "/ifindex");
    std::uint32_t observed{};
    identity >> observed;
    std::string qdisc;
    if (observed != expected_ifindex ||
        run({"tc", "qdisc", "show", "dev", interface}, &qdisc) != 0 || qdisc != expected_qdisc)
      ::_exit(0);
    ::execlp("tc", "tc", "qdisc", "del", "dev", interface.c_str(), "root",
             static_cast<char*>(nullptr));
    ::_exit(errno == ENOENT ? 127 : 126);
  }
  ::close(gate[0]);
  try {
    std::string start;
    for (int attempt = 0; attempt < 100; ++attempt) {
      start = process_start_time(static_cast<std::uint32_t>(child));
      if (timer_owned(static_cast<std::uint32_t>(child), start)) break;
      ::usleep(10000);
    }
    if (!timer_owned(static_cast<std::uint32_t>(child), start))
      throw std::runtime_error("cannot record fault timer identity");
    registered(static_cast<std::uint32_t>(child), start);
    if (::write(gate[1], "1", 1) != 1) throw std::runtime_error("cannot release owned fault timer");
    ::close(gate[1]);
  } catch (...) {
    ::close(gate[1]);
    throw;
  }
  return static_cast<std::uint32_t>(child);
}

std::string qdisc_state(const std::string& interface) {
  std::string value;
  if (run({"tc", "qdisc", "show", "dev", interface}, &value) != 0) return {};
  return value;
}

OwnedFault create_fault(const ExpectedFault& expected,
                        const std::function<void(const OwnedFault&)>& register_identity) {
  const auto& definition = expected.definition;
  const auto ifindex = link_ifindex(expected.interface);
  if (!ifindex) throw std::runtime_error("fault target interface is missing");
  if (qdisc_state(expected.interface).find("netem") != std::string::npos)
    throw std::runtime_error("refusing pre-existing netem qdisc on " + expected.interface);
  std::vector<std::string> command = {"tc",   "qdisc", "add", "dev", expected.interface,
                                      "root", "netem"};
  if (definition.delay_ms) {
    command.insert(command.end(), {"delay", std::to_string(definition.delay_ms) + "ms"});
    if (definition.jitter_ms) command.push_back(std::to_string(definition.jitter_ms) + "ms");
  }
  if (definition.loss_percent > 0.0) {
    std::ostringstream value;
    value << definition.loss_percent << '%';
    command.insert(command.end(), {"loss", value.str()});
  }
  if (definition.rate_kbit)
    command.insert(command.end(), {"rate", std::to_string(definition.rate_kbit) + "kbit"});
  if (run(command) != 0) throw std::runtime_error("cannot apply declarative netem fault");
  OwnedFault fault;
  fault.id = definition.id;
  fault.attachment_id = definition.attachment;
  fault.interface = expected.interface;
  fault.ifindex = *ifindex;
  fault.qdisc_identity = qdisc_state(expected.interface);
  if (fault.qdisc_identity.find("netem") == std::string::npos)
    throw std::runtime_error("cannot record declarative netem identity");
  try {
    fault.boot_id = boot_identity();
    fault.applied_monotonic_ns = monotonic_nanoseconds();
    fault.expires_monotonic_ns =
        fault.applied_monotonic_ns +
        static_cast<std::uint64_t>(definition.duration_seconds) * 1'000'000'000ULL;
    if (fault.boot_id.empty() || fault.expires_monotonic_ns <= fault.applied_monotonic_ns)
      throw std::runtime_error("cannot record fault expiry identity");
    fault.timer_pid =
        spawn_fault_timer(definition.duration_seconds, expected.interface, fault.ifindex,
                          fault.qdisc_identity, [&](std::uint32_t pid, const std::string& start) {
                            fault.timer_pid = pid;
                            fault.timer_start_time = start;
                            if (register_identity) register_identity(fault);
                          });
  } catch (...) {
    if (fault.timer_pid) {
      const auto current_start = process_start_time(fault.timer_pid);
      if (timer_owned(fault.timer_pid, current_start))
        stop_owned_process(fault.timer_pid, current_start, {}, true);
    }
    if (link_ifindex(expected.interface) == fault.ifindex &&
        qdisc_state(expected.interface) == fault.qdisc_identity)
      run({"tc", "qdisc", "del", "dev", expected.interface, "root"});
    throw;
  }
  return fault;
}

bool fault_deadline_elapsed(const OwnedFault& fault) {
  return boot_identity() == fault.boot_id && monotonic_nanoseconds() >= fault.expires_monotonic_ns;
}

bool fault_healthy(const OwnedFault& fault) {
  if (link_ifindex(fault.interface) != fault.ifindex) return false;
  const auto qdisc = qdisc_state(fault.interface);
  if (timer_owned(fault.timer_pid, fault.timer_start_time)) return qdisc == fault.qdisc_identity;
  return fault_deadline_elapsed(fault) && qdisc.find("netem") == std::string::npos;
}

bool clear_fault(const OwnedFault& fault) {
  if (link_ifindex(fault.interface) != fault.ifindex) return false;
  const bool timer_active = timer_owned(fault.timer_pid, fault.timer_start_time);
  const auto qdisc = qdisc_state(fault.interface);
  if (timer_active && qdisc != fault.qdisc_identity) return false;
  if (!timer_active && (!fault_deadline_elapsed(fault) || qdisc.find("netem") != std::string::npos))
    return false;
  if (timer_active && !stop_owned_process(fault.timer_pid, fault.timer_start_time, {}, true))
    return false;
  return qdisc.find("netem") == std::string::npos ||
         run({"tc", "qdisc", "del", "dev", fault.interface, "root"}) == 0;
}

}  // namespace graphx::infra::detail
