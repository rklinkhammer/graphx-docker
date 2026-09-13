#pragma once
#include <algorithm>
#include <cerrno>
#include <string_view>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace graphx::detail {
// Drain application output inside the application process. History/capture files
// do not share the log quota, unlike an RLIMIT_FSIZE applied to the whole process.
inline void bound_process_output() {
  static std::once_flag once;
  std::call_once(once, [] {
    const auto* text = std::getenv("GRAPHX_LOG_MAX_BYTES");
    if (!text) return;
    std::size_t limit{};
    const std::string_view value(text);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), limit);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || limit < 4096 ||
        limit > 16 * 1024 * 1024)
      throw std::runtime_error("E_LOG_BOUND: invalid process output bound");
    struct Drain {
      int input{};
      int output{};
      std::thread worker;
    };
    int pipe[2]{};
    const int log = ::dup(STDOUT_FILENO);
    if (log < 0 || ::pipe(pipe) != 0) throw std::runtime_error("E_LOG_BOUND: pipe creation failed");
    auto* drain = new Drain{pipe[0], log, {}};
    if (::dup2(pipe[1], STDOUT_FILENO) < 0 || ::dup2(pipe[1], STDERR_FILENO) < 0)
      throw std::runtime_error("E_LOG_BOUND: stream redirection failed");
    ::close(pipe[1]);
    drain->worker = std::thread([drain, limit] {
      std::array<char, 4096> bytes{};
      std::size_t written{};
      for (;;) {
        const auto count = ::read(drain->input, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        const auto keep = std::min(static_cast<std::size_t>(count), limit - written);
        std::size_t offset{};
        while (offset < keep) {
          const auto size = ::write(drain->output, bytes.data() + offset, keep - offset);
          if (size < 0 && errno == EINTR) continue;
          if (size <= 0) break;
          offset += static_cast<std::size_t>(size);
        }
        written += keep;
      }
      ::close(drain->input);
      ::close(drain->output);
    });
    static Drain* finalizer = drain;
    std::atexit([] {
      std::cout.flush();
      std::cerr.flush();
      std::fflush(nullptr);
      ::close(STDOUT_FILENO);
      ::close(STDERR_FILENO);
      finalizer->worker.join();
      delete finalizer;
    });
  });
}
}  // namespace graphx::detail
