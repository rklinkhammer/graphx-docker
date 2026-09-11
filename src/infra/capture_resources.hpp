#pragma once

#include "infra/ownership_state.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace graphx::infra::detail {

void detach_child_io();
std::string process_start_time(std::uint32_t pid);
bool process_owned(std::uint32_t pid, const std::string& start_time, std::string_view marker);
bool timer_owned(std::uint32_t pid, const std::string& start_time);
bool stop_owned_process(std::uint32_t pid, const std::string& start_time, std::string_view marker,
                        bool timer = false);
bool capture_directory_metadata_matches(const OwnedCapture& capture, const struct stat& metadata,
                                        std::uint32_t required_mode = 0700);
bool directory_identity_matches(const OwnedCapture& capture);
bool capture_file_metadata_is_safe(const OwnedCapture& capture, const struct stat& metadata);
OwnedCapture create_capture(const ExpectedCapture& expected, const OwnershipState& state);
bool capture_healthy(const ExpectedCapture& expected, const OwnedCapture& capture);
bool stop_capture(const OwnedCapture& capture);
bool complete_pcapng_snapshot(int descriptor, std::uint64_t size);

}  // namespace graphx::infra::detail
