#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sys/socket.h>

namespace graphx::socket_detail {

using Clock = std::chrono::steady_clock;

inline int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

inline int poll_timeout(Clock::time_point deadline, bool has_deadline) {
  if (!has_deadline) return -1;
  const auto remaining = deadline - Clock::now();
  if (remaining <= Clock::duration::zero()) return 0;
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      remaining + std::chrono::milliseconds(1));
  return static_cast<int>(
      std::min<std::int64_t>(milliseconds.count(), std::numeric_limits<int>::max()));
}

}  // namespace graphx::socket_detail
