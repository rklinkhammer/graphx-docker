#include "infra/compose_execution.hpp"
#include "infra/lifecycle_coordinator.hpp"
#include "infra/process_resources.hpp"
#include "config_document.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace graphx::infra::detail {
namespace {
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
}  // namespace
int execute_capture_handoff(const ExecutionOptions& options, const ConfigValue& resolved,
                            std::ostream& output) {
  if (options.owner_token.empty()) throw std::runtime_error("E_CAPTURE_HANDOFF: missing owner");
  GraphConfig config;
  config.version = 3;
  config.id = resolved.at("graph_id").text();
  const auto root = options.state_root / config.id;
  const auto state_file = root / "ownership.yml";
  const auto manifest = options.output / "compile-manifest.json";
  const auto hash = configuration_hash(manifest);
  stopped = 0;
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  std::size_t diagnostics{};
  while (!stopped) {
    {
      std::optional<OwnershipLock> lock;
      try {
        lock.emplace(OwnershipLock::open_existing(root / ".lock", OwnershipLockMode::exclusive));
      } catch (const std::runtime_error& error) {
        if (std::string_view(error.what()) !=
            "another infrastructure operation owns the graph lock")
          throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      auto state = load_state(state_file);
      if (state.graph_id != config.id || state.owner_token != options.owner_token ||
          state.config_hash != hash)
        throw std::runtime_error("E_CAPTURE_HANDOFF: ownership changed");
      const auto self = std::ranges::find_if(state.processes, [](const auto& p) {
        return p.kind == "native" && p.name == "mg-capture-handoff" &&
               p.stable_id == std::to_string(::getpid());
      });
      if (self == state.processes.end() || !native_process_status(*self))
        throw std::runtime_error("E_CAPTURE_HANDOFF: exporter is not registered");
      if (state.status != "ready") return 0;
      const auto directory = root / state.handoff_name;
      safe_execution_path(directory);
      struct stat metadata{};
      if (::lstat(directory.c_str(), &metadata) != 0 || !S_ISDIR(metadata.st_mode) ||
          metadata.st_uid != ::geteuid() || (metadata.st_mode & 0777) != 0755 ||
          static_cast<std::uint64_t>(metadata.st_ino) != state.handoff_inode ||
          static_cast<std::uint64_t>(metadata.st_dev) != state.handoff_device)
        throw std::runtime_error("E_CAPTURE_HANDOFF: directory replaced");
      for (auto& capture : state.captures) {
        const auto destination = directory / (capture.id + ".pcapng");
        const auto pending = directory / (capture.id + ".pending");
        safe_execution_path(destination, true);
        safe_execution_path(pending, true);
        if (::lstat(destination.c_str(), &metadata) == 0) {
          const auto inode = static_cast<std::uint64_t>(metadata.st_ino);
          if (static_cast<std::uint64_t>(metadata.st_dev) != state.handoff_device ||
              metadata.st_uid != ::geteuid() || (metadata.st_mode & 0777) != 0444 ||
              (inode != capture.snapshot_inode && inode != capture.snapshot_pending_inode))
            throw std::runtime_error("E_CAPTURE_HANDOFF: snapshot replaced");
          capture.snapshot_inode = inode;
          capture.snapshot_pending_inode = 0;
          save_state(state_file, state);
        } else if (capture.snapshot_inode) {
          throw std::runtime_error("E_CAPTURE_HANDOFF: snapshot missing");
        }
        if (std::filesystem::exists(pending))
          throw std::runtime_error("E_CAPTURE_HANDOFF: interrupted export retained for inspection");
        std::ostringstream exported;
        try {
          export_owned_network_capture_impl(config, manifest, capture.id, options.state_root,
                                            pending, exported, &state);
        } catch (const std::exception& error) {
          if (std::filesystem::exists(pending)) throw;
          if (++diagnostics <= 4096) output << error.what() << std::endl;
          continue;  // A live ring can end in a partial block; retain the last sealed snapshot.
        }
        if (::chmod(pending.c_str(), 0444) != 0 || ::lstat(pending.c_str(), &metadata) != 0)
          throw std::runtime_error("E_CAPTURE_HANDOFF: cannot seal snapshot");
        capture.snapshot_pending_inode = static_cast<std::uint64_t>(metadata.st_ino);
        save_state(state_file, state);
        std::filesystem::rename(pending, destination);
        capture.snapshot_inode = capture.snapshot_pending_inode;
        capture.snapshot_pending_inode = 0;
        save_state(state_file, state);
      }
    }
    for (int i = 0; i < 50 && !stopped; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return 0;
}
}  // namespace graphx::infra::detail
