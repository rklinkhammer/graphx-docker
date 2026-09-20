#pragma once
#include <cstddef>
#include <cstdint>
namespace graphx::vita {
struct RecorderCounters {
  std::uint64_t packets{}, bytes{}, truncated{}, invalid{}, kernel_drops{}, errors{}, log_drops{};
  void receive(std::size_t reported, std::size_t capacity) noexcept;
};
// Linux-only, irreversible process-wide receive-descriptor restriction.
void restrict_recorder_descriptor(int descriptor);
int run_recorder(int argc, char** argv);
}  // namespace graphx::vita
