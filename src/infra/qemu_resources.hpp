#pragma once
#include "graphx/execution.hpp"
#include "graphx/config.hpp"
#include "infra/process_resources.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace graphx::infra::detail {
ConfigValue inspect_guest(const OwnedResourceIdentity& process, bool pause_resume,
                          bool console_bytes = false);
ConfigValue verify_guest_artifacts(const ExecutionOptions& options, const ConfigValue& resolved);
void require_guest_account();
void verify_guest_directory(const OwnedResourceIdentity& resource);
void cleanup_guest_directory(const OwnedResourceIdentity& resource);
class GuestSession {
 public:
  GuestSession(const ExecutionOptions& options, const ConfigValue& guest, const ConfigValue& node,
               const ConfigValue& credentials, const std::filesystem::path& directory,
               const std::string& token, std::chrono::steady_clock::time_point deadline,
               const std::function<void(const OwnedResourceIdentity&)>& register_identity,
               const std::function<bool()>& cancelled);
  ~GuestSession();
  GuestSession(const GuestSession&) = delete;
  GuestSession& operator=(const GuestSession&) = delete;
  void release();

 private:
  friend ConfigValue inspect_guest(const OwnedResourceIdentity&, bool, bool);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace graphx::infra::detail
