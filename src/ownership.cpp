#include "graphx/ownership.hpp"

#include <yaml-cpp/yaml.h>

#include <openssl/rand.h>
#include <openssl/evp.h>

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <unordered_set>

namespace graphx {
namespace {

struct OwnershipState {
  std::string graph_id;
  std::string config_hash;
  std::string owner_token;
  std::string status;
  std::vector<std::string> expected_bridges;
  std::vector<OwnedResourceIdentity> bridges;
};

OwnedResourceIdentity bridge_identity(std::string name, std::string uuid) {
  OwnedResourceIdentity identity;
  identity.kind = "ovs_bridge";
  identity.name = std::move(name);
  identity.stable_id = std::move(uuid);
  return identity;
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int value = -1) : value_(value) {}
  ~FileDescriptor() {
    if (value_ >= 0) ::close(value_);
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  [[nodiscard]] int get() const noexcept { return value_; }

 private:
  int value_;
};

std::string hex(const unsigned char* data, std::size_t size) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < size; ++index)
    output << std::setw(2) << unsigned(data[index]);
  return output.str();
}

std::string configuration_hash(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open configuration for hashing: " + path.string());
  EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
  if (!raw_context) throw std::runtime_error("cannot allocate configuration hash context");
  const auto cleanup = [&] { EVP_MD_CTX_free(raw_context); };
  if (EVP_DigestInit_ex(raw_context, EVP_sha256(), nullptr) != 1) {
    cleanup();
    throw std::runtime_error("cannot initialize configuration hash");
  }
  std::array<char, 8192> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (input.gcount() > 0)
      if (EVP_DigestUpdate(raw_context, buffer.data(), static_cast<std::size_t>(input.gcount())) !=
          1) {
        cleanup();
        throw std::runtime_error("cannot update configuration hash");
      }
  }
  if (!input.eof()) {
    cleanup();
    throw std::runtime_error("cannot read configuration for hashing");
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size{};
  if (EVP_DigestFinal_ex(raw_context, digest.data(), &digest_size) != 1) {
    cleanup();
    throw std::runtime_error("cannot finalize configuration hash");
  }
  cleanup();
  return hex(digest.data(), digest_size);
}

std::string random_token() {
  std::array<unsigned char, 16> bytes{};
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    throw std::runtime_error("cannot generate ownership token");
  return hex(bytes.data(), bytes.size());
}

int run(const std::vector<std::string>& arguments, std::string* captured = nullptr) {
  int pipefd[2] = {-1, -1};
  if (captured && ::pipe(pipefd) != 0) throw std::system_error(errno, std::generic_category());
  const auto child = ::fork();
  if (child < 0) throw std::system_error(errno, std::generic_category());
  if (child == 0) {
    if (captured) {
      ::close(pipefd[0]);
      ::dup2(pipefd[1], STDOUT_FILENO);
      ::close(pipefd[1]);
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }
  if (captured) {
    ::close(pipefd[1]);
    captured->clear();
    std::array<char, 512> buffer{};
    for (;;) {
      const auto count = ::read(pipefd[0], buffer.data(), buffer.size());
      if (count > 0)
        captured->append(buffer.data(), static_cast<std::size_t>(count));
      else if (count < 0 && errno == EINTR)
        continue;
      else
        break;
    }
    ::close(pipefd[0]);
  }
  int status{};
  if (::waitpid(child, &status, 0) < 0) throw std::system_error(errno, std::generic_category());
  if (captured)
    while (!captured->empty() && (captured->back() == '\n' || captured->back() == '\r'))
      captured->pop_back();
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

std::string ovs_get(const std::string& bridge, const std::string& column) {
  std::string value;
  if (run({"ovs-vsctl", "get", "Bridge", bridge, column}, &value) != 0) return {};
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  return value;
}

bool bridge_exists(const std::string& name) { return run({"ovs-vsctl", "br-exists", name}) == 0; }

void ensure_state_root(const std::filesystem::path& root) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(root, error);
  if (!error && std::filesystem::is_symlink(status))
    throw std::runtime_error("ownership state root must not be a symlink");
  if (!std::filesystem::exists(status)) {
    if (!std::filesystem::create_directories(root, error) || error)
      throw std::runtime_error("cannot create ownership state root: " + error.message());
    if (::chmod(root.c_str(), 0700) != 0)
      throw std::system_error(errno, std::generic_category(), "cannot secure ownership state root");
  } else if (!std::filesystem::is_directory(status)) {
    throw std::runtime_error("ownership state root is not a directory");
  } else {
    struct stat metadata{};
    if (::lstat(root.c_str(), &metadata) != 0)
      throw std::system_error(errno, std::generic_category(),
                              "cannot inspect ownership state root");
    if ((metadata.st_mode & 0777) != 0700)
      throw std::runtime_error("existing ownership state root permissions must be 0700");
  }
}

bool path_entry_exists(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (!error) return status.type() != std::filesystem::file_type::not_found;
  if (error == std::errc::no_such_file_or_directory) return false;
  throw std::runtime_error("cannot inspect ownership path " + path.string() + ": " +
                           error.message());
}

bool inspect_existing_state_root(const std::filesystem::path& root) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(root, error);
  if (error == std::errc::no_such_file_or_directory ||
      (!error && status.type() == std::filesystem::file_type::not_found))
    return false;
  if (error) throw std::runtime_error("cannot inspect ownership state root: " + error.message());
  if (std::filesystem::is_symlink(status))
    throw std::runtime_error("ownership state root must not be a symlink");
  if (!std::filesystem::is_directory(status))
    throw std::runtime_error("ownership state root is not a directory");
  struct stat metadata{};
  if (::lstat(root.c_str(), &metadata) != 0)
    throw std::system_error(errno, std::generic_category(), "cannot inspect ownership state root");
  if ((metadata.st_mode & 0777) != 0700)
    throw std::runtime_error("existing ownership state root permissions must be 0700");
  return true;
}

YAML::Node state_node(const OwnershipState& state) {
  YAML::Node root;
  root["version"] = 1;
  root["phase"] = "M3";
  root["graph_id"] = state.graph_id;
  root["config_sha256"] = state.config_hash;
  root["owner_token"] = state.owner_token;
  root["status"] = state.status;
  for (const auto& name : state.expected_bridges) root["expected_bridges"].push_back(name);
  for (const auto& bridge : state.bridges) {
    YAML::Node item;
    item["kind"] = bridge.kind;
    item["name"] = bridge.name;
    item["uuid"] = bridge.stable_id;
    root["resources"].push_back(item);
  }
  return root;
}

void save_state(const std::filesystem::path& path, const OwnershipState& state,
                bool replace_existing = true) {
  YAML::Emitter emitter;
  emitter.SetIndent(2);
  emitter << state_node(state);
  if (!emitter.good()) throw std::runtime_error("cannot serialize ownership state");
  const auto temporary = path.string() + ".tmp." + std::to_string(::getpid());
  const FileDescriptor descriptor(
      ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600));
  if (descriptor.get() < 0)
    throw std::system_error(errno, std::generic_category(), "cannot create temporary state");
  const std::string bytes = std::string(emitter.c_str()) + '\n';
  std::size_t offset{};
  while (offset < bytes.size()) {
    const auto count = ::write(descriptor.get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      ::unlink(temporary.c_str());
      throw std::system_error(errno == 0 ? EIO : errno, std::generic_category(),
                              "cannot write ownership state");
    }
    offset += static_cast<std::size_t>(count);
  }
  if (::fsync(descriptor.get()) != 0) {
    const auto failure = errno;
    ::unlink(temporary.c_str());
    throw std::system_error(failure, std::generic_category(), "cannot publish ownership state");
  }
  if (replace_existing) {
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
      const auto failure = errno;
      ::unlink(temporary.c_str());
      throw std::system_error(failure, std::generic_category(), "cannot publish ownership state");
    }
  } else {
    // link(2) is an atomic no-replace publication: EEXIST also covers dangling
    // symlinks, unlike std::filesystem::exists().
    if (::link(temporary.c_str(), path.c_str()) != 0) {
      const auto failure = errno;
      ::unlink(temporary.c_str());
      throw std::system_error(failure, std::generic_category(),
                              "cannot exclusively publish ownership state");
    }
    ::unlink(temporary.c_str());
  }
  const FileDescriptor directory(
      ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  if (directory.get() < 0 || ::fsync(directory.get()) != 0)
    throw std::system_error(errno, std::generic_category(),
                            "cannot sync ownership state directory");
}

std::string required_scalar(const YAML::Node& root, std::string_view key) {
  const auto value = root[std::string(key)];
  if (!value || !value.IsScalar() || value.Scalar().empty())
    throw std::runtime_error("invalid ownership state field: " + std::string(key));
  return value.Scalar();
}

OwnershipState load_state(const std::filesystem::path& path) {
  const FileDescriptor descriptor(::open(path.c_str(), O_RDONLY | O_NOFOLLOW));
  if (descriptor.get() < 0)
    throw std::system_error(errno, std::generic_category(), "cannot open ownership state");
  struct stat metadata{};
  if (::fstat(descriptor.get(), &metadata) != 0)
    throw std::system_error(errno, std::generic_category(), "cannot inspect ownership state");
  if (!S_ISREG(metadata.st_mode))
    throw std::runtime_error("ownership state must be a regular file");
  if ((metadata.st_mode & 0777) != 0600)
    throw std::runtime_error("ownership state permissions must be 0600");
  if (metadata.st_size < 0 || metadata.st_size > 1024 * 1024)
    throw std::runtime_error("ownership state exceeds 1 MiB");
  std::string bytes(static_cast<std::size_t>(metadata.st_size), '\0');
  std::size_t offset{};
  while (offset < bytes.size()) {
    const auto count = ::read(descriptor.get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw std::runtime_error("cannot read complete ownership state");
    offset += static_cast<std::size_t>(count);
  }
  const auto root = YAML::Load(bytes);
  if (!root.IsMap() || root["version"].as<int>(0) != 1 || required_scalar(root, "phase") != "M3")
    throw std::runtime_error("unsupported ownership state format");
  OwnershipState state;
  state.graph_id = required_scalar(root, "graph_id");
  state.config_hash = required_scalar(root, "config_sha256");
  state.owner_token = required_scalar(root, "owner_token");
  state.status = required_scalar(root, "status");
  const auto hexadecimal = [](std::string_view value) {
    for (const auto character : value)
      if (!std::isxdigit(static_cast<unsigned char>(character))) return false;
    return true;
  };
  if (state.config_hash.size() != 64 || !hexadecimal(state.config_hash) ||
      state.owner_token.size() != 32 || !hexadecimal(state.owner_token))
    throw std::runtime_error("invalid ownership state identity");
  if (state.status != "creating" && state.status != "ready" && state.status != "destroying")
    throw std::runtime_error("invalid ownership state status");
  if (!root["expected_bridges"] || !root["expected_bridges"].IsSequence())
    throw std::runtime_error("invalid ownership state expected_bridges");
  std::unordered_set<std::string> expected;
  for (const auto& value : root["expected_bridges"]) {
    const auto name = value.as<std::string>();
    if (name.empty() || !expected.insert(name).second)
      throw std::runtime_error("invalid or duplicate expected bridge");
    state.expected_bridges.push_back(name);
  }
  std::unordered_set<std::string> recorded;
  if (root["resources"])
    for (const auto& value : root["resources"]) {
      if (required_scalar(value, "kind") != "ovs_bridge")
        throw std::runtime_error("unsupported owned resource kind");
      const auto name = required_scalar(value, "name");
      const auto uuid = required_scalar(value, "uuid");
      if (!expected.contains(name) || !recorded.insert(name).second || uuid.size() != 36)
        throw std::runtime_error("invalid owned bridge record");
      state.bridges.push_back(bridge_identity(name, uuid));
    }
  return state;
}

std::string planned_create(const SwitchDefinition& network_switch, std::string_view graph_id,
                           std::string_view token, std::string_view hash) {
  return "ovs-vsctl -- add-br " + network_switch.id + " -- set Bridge " + network_switch.id +
         " datapath_type=system external_ids:graphx_owner=" + std::string(token) +
         " external_ids:graphx_config_hash=" + std::string(hash) +
         " external_ids:graphx_graph=" + std::string(graph_id);
}

bool bridge_owned(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  return bridge_exists(bridge.name) && ovs_get(bridge.name, "_uuid") == bridge.stable_id &&
         ovs_get(bridge.name, "external_ids:graphx_owner") == state.owner_token &&
         ovs_get(bridge.name, "external_ids:graphx_config_hash") == state.config_hash &&
         ovs_get(bridge.name, "external_ids:graphx_graph") == state.graph_id;
}

bool delete_owned_bridge(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  return run({"ovs-vsctl", "--timeout=2", "--", "wait-until", "Bridge", bridge.stable_id,
              "external_ids:graphx_owner=" + state.owner_token,
              "external_ids:graphx_config_hash=" + state.config_hash,
              "external_ids:graphx_graph=" + state.graph_id, "--", "remove", "Open_vSwitch", ".",
              "bridges", bridge.stable_id, "--", "destroy", "Bridge", bridge.stable_id}) == 0;
}

}  // namespace

std::filesystem::path default_ownership_state_root() {
  if (const auto* configured = std::getenv("GRAPHX_STATE_DIR")) return configured;
  return "/var/lib/graphx/runs";
}

int execute_ovs_lifecycle(const GraphConfig& config, const std::filesystem::path& config_path,
                          OvsLifecycleAction action, bool dry_run,
                          const std::filesystem::path& state_root, std::ostream& output,
                          std::ostream& errors) {
  if (config.version != 2) throw std::invalid_argument("M3 OVS lifecycle requires version 2");
  const auto hash = configuration_hash(config_path);
  const auto state_path = state_root / (config.id + ".yaml");
  if (dry_run) {
    if (action == OvsLifecycleAction::create) {
      output << "# ownership-state " << state_path << "\n";
      for (const auto& item : config.network_infrastructure.switches)
        output << planned_create(item, config.id, "<generated-owner-token>", hash) << '\n';
    } else {
      output << "# " << (action == OvsLifecycleAction::status ? "inspect" : "identity-check-delete")
             << " ownership-state " << state_path << '\n';
    }
    return 0;
  }

  const auto lock_path = state_root / (config.id + ".lock");
  if (action == OvsLifecycleAction::status) {
    if (!inspect_existing_state_root(state_root) || !path_entry_exists(state_path)) {
      output << "No M3 ownership state for graph " << config.id << '\n';
      return 2;
    }
    FileDescriptor lock(::open(lock_path.c_str(), O_RDONLY | O_NOFOLLOW));
    if (lock.get() < 0)
      throw std::system_error(errno, std::generic_category(),
                              "cannot open existing ownership lock");
    struct stat lock_metadata{};
    if (::fstat(lock.get(), &lock_metadata) != 0 || !S_ISREG(lock_metadata.st_mode))
      throw std::runtime_error("ownership lock must be a regular file");
    if ((lock_metadata.st_mode & 0777) != 0600)
      throw std::runtime_error("ownership lock permissions must be 0600");
    if (::flock(lock.get(), LOCK_SH | LOCK_NB) != 0)
      throw std::runtime_error("another infrastructure operation owns the graph lock");
    const auto state = load_state(state_path);
    if (state.graph_id != config.id) throw std::runtime_error("ownership state graph mismatch");
    const bool config_matches = state.config_hash == hash;
    if (!config_matches)
      errors << "graphx: current configuration differs from the ownership ledger; resource "
                "identity still controls cleanup\n";
    bool healthy = state.status == "ready" && config_matches;
    output << "graph=" << state.graph_id << " status=" << state.status
           << " config-sha256=" << state.config_hash
           << " current-config=" << (config_matches ? "matched" : "drifted") << '\n';
    for (const auto& item : state.bridges) {
      const bool owned = bridge_owned(item, state);
      output << "ovs_bridge " << item.name << " uuid=" << item.stable_id
             << " state=" << (owned ? "owned" : "missing-or-replaced") << '\n';
      healthy = healthy && owned;
    }
    return healthy ? 0 : 2;
  }

  ensure_state_root(state_root);
  // Refuse even a dangling symlink before create mutates the per-graph state
  // directory by opening or creating its lock.
  if (action == OvsLifecycleAction::create && path_entry_exists(state_path))
    throw std::runtime_error("ownership state already exists; inspect, destroy, or recover it");
  FileDescriptor lock(::open(lock_path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW, 0600));
  if (lock.get() < 0) throw std::system_error(errno, std::generic_category(), "cannot open lock");
  struct stat lock_metadata{};
  if (::fstat(lock.get(), &lock_metadata) != 0 || !S_ISREG(lock_metadata.st_mode))
    throw std::runtime_error("ownership lock must be a regular file");
  if (::fchmod(lock.get(), 0600) != 0)
    throw std::system_error(errno, std::generic_category(), "cannot secure ownership lock");
  if (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0)
    throw std::runtime_error("another infrastructure operation owns the graph lock");

  if (action == OvsLifecycleAction::create) {
    if (path_entry_exists(state_path))
      throw std::runtime_error("ownership state already exists; inspect, destroy, or recover it");
    for (const auto& item : config.network_infrastructure.switches)
      if (bridge_exists(item.id))
        throw std::runtime_error("refusing unowned OVS bridge collision: " + item.id);
    OwnershipState state{config.id, hash, random_token(), "creating", {}, {}};
    for (const auto& item : config.network_infrastructure.switches)
      state.expected_bridges.push_back(item.id);
    save_state(state_path, state, false);
    try {
      std::size_t mutation{};
      for (const auto& item : config.network_infrastructure.switches) {
        std::string uuid;
        const std::vector<std::string> command = {
            "ovs-vsctl",
            "--",
            "add-br",
            item.id,
            "--",
            "set",
            "Bridge",
            item.id,
            "datapath_type=system",
            "external_ids:graphx_owner=" + state.owner_token,
            "external_ids:graphx_config_hash=" + state.config_hash,
            "external_ids:graphx_graph=" + state.graph_id};
        output << "+ " << planned_create(item, state.graph_id, state.owner_token, state.config_hash)
               << '\n';
        if (run(command) != 0)
          throw std::runtime_error("cannot atomically create owned OVS bridge " + item.id);
        uuid = ovs_get(item.id, "_uuid");
        if (uuid.empty()) throw std::runtime_error("cannot capture owned OVS bridge identity");
        ++mutation;
        if (const auto* crash = std::getenv("GRAPHX_M3_CRASH_AFTER");
            crash && std::to_string(mutation) == crash)
          ::_exit(99);  // Deliberate verifier-only crash point; the state enables recovery.
        if (const auto* failure = std::getenv("GRAPHX_M3_FAIL_AFTER");
            failure && std::to_string(mutation) == failure)
          throw std::runtime_error("injected M3 interruption after mutation " +
                                   std::to_string(mutation));
        state.bridges.push_back(bridge_identity(item.id, uuid));
        save_state(state_path, state);
      }
      state.status = "ready";
      save_state(state_path, state);
      output << "GraphX M3 OVS state ready: " << state_path << '\n';
      return 0;
    } catch (...) {
      errors << "graphx: create interrupted; recovering owned OVS mutations\n";
      // Include atomic mutations that happened before their ledger update.
      for (const auto& name : state.expected_bridges) {
        bool recorded{};
        for (const auto& item : state.bridges) recorded = recorded || item.name == name;
        if (recorded) continue;
        if (!bridge_exists(name)) continue;
        const auto discovered = bridge_identity(name, ovs_get(name, "_uuid"));
        if (ovs_get(name, "external_ids:graphx_owner") == state.owner_token &&
            ovs_get(name, "external_ids:graphx_config_hash") == state.config_hash &&
            ovs_get(name, "external_ids:graphx_graph") == state.graph_id)
          state.bridges.push_back(discovered);
      }
      bool cleanup_complete = true;
      for (auto iterator = state.bridges.rbegin(); iterator != state.bridges.rend(); ++iterator)
        if (bridge_exists(iterator->name)) {
          if (!bridge_owned(*iterator, state) || !delete_owned_bridge(*iterator, state)) {
            cleanup_complete = false;
            errors << "graphx: retained ownership state after rollback could not safely remove "
                   << iterator->name << '\n';
          }
        }
      if (cleanup_complete)
        std::filesystem::remove(state_path);
      else
        save_state(state_path, state);
      throw;
    }
  }

  if (!path_entry_exists(state_path)) {
    throw std::runtime_error("no M3 ownership state for graph " + config.id);
  }
  auto state = load_state(state_path);
  if (state.graph_id != config.id) throw std::runtime_error("ownership state graph mismatch");
  const bool config_matches = state.config_hash == hash;
  if (!config_matches)
    errors << "graphx: current configuration differs from the ownership ledger; resource identity "
              "still controls cleanup\n";

  if (action == OvsLifecycleAction::recover && state.status == "ready")
    throw std::runtime_error("ownership state is ready; use infra destroy, not recover");

  // Recover an atomic bridge mutation that completed before its state update.
  for (const auto& name : state.expected_bridges) {
    bool recorded{};
    for (const auto& item : state.bridges) recorded = recorded || item.name == name;
    if (recorded || !bridge_exists(name)) continue;
    if (ovs_get(name, "external_ids:graphx_owner") != state.owner_token ||
        ovs_get(name, "external_ids:graphx_config_hash") != state.config_hash ||
        ovs_get(name, "external_ids:graphx_graph") != state.graph_id)
      throw std::runtime_error("refusing unowned or replaced OVS bridge: " + name);
    state.bridges.push_back(bridge_identity(name, ovs_get(name, "_uuid")));
  }
  // Refuse the entire cleanup before the first mutation when any recorded
  // resource has been replaced. Each item is checked again immediately before
  // its own deletion to close the remaining race window.
  for (const auto& item : state.bridges)
    if (bridge_exists(item.name) && !bridge_owned(item, state))
      throw std::runtime_error("refusing to delete replaced OVS bridge: " + item.name);
  state.status = "destroying";
  save_state(state_path, state);
  for (auto iterator = state.bridges.rbegin(); iterator != state.bridges.rend(); ++iterator) {
    if (!bridge_exists(iterator->name)) continue;
    if (!bridge_owned(*iterator, state))
      throw std::runtime_error("refusing to delete replaced OVS bridge: " + iterator->name);
    output << "- ovs-vsctl del-br " << iterator->name << '\n';
    if (!delete_owned_bridge(*iterator, state))
      throw std::runtime_error("identity changed while deleting owned OVS bridge " +
                               iterator->name);
  }
  std::filesystem::remove(state_path);
  output << "GraphX M3 OVS state removed\n";
  return 0;
}

}  // namespace graphx
