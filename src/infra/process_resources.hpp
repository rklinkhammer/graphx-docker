#pragma once
#include "infra/command_runner.hpp"
#include "infra/ownership_state.hpp"
#include <functional>
#include <map>

namespace graphx::infra::detail {
struct NativeProcessOptions {
  std::string id;
  std::vector<std::string> argv;
  std::map<std::string, std::string> environment;
  std::filesystem::path cwd;
  std::filesystem::path log;
  std::filesystem::path executable;
  std::filesystem::path network_namespace;
  std::uint64_t namespace_inode{};
  bool guest_identity{};
  // QEMU receives only this directory capability at descriptor 198 on Linux.
  std::filesystem::path console_directory;
  std::uint64_t log_bytes{2097152};
  std::uint64_t file_bytes_limit{};
};
// The child cannot exec before its identity has been durably registered.
OwnedResourceIdentity start_native_process(
    const NativeProcessOptions& options,
    const std::function<void(const OwnedResourceIdentity&)>& register_identity);
// 0: absent, 1: exact identity, throws on substitution.
int native_process_status(const OwnedResourceIdentity& resource);
void stop_native_process(const OwnedResourceIdentity& resource);
void require_available_tcp_port(std::uint16_t port);
void publish_execution_file(const std::filesystem::path& path, const std::string& bytes,
                            unsigned mode = 0600);
void safe_execution_path(const std::filesystem::path& path, bool absent = false);
}  // namespace graphx::infra::detail
