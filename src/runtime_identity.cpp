#include "graphx/runtime_identity.hpp"

#include "infra/ownership_lock.hpp"
#include "infra/ownership_state.hpp"

#include <yaml-cpp/yaml.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <unordered_set>

namespace graphx {
bool valid_execution_id(std::string_view value) noexcept {
  if (value.size() != 32 || value.find_first_not_of('0') == std::string_view::npos) return false;
  for (const char ch : value)
    if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f')) return false;
  return true;
}
void validate_execution_identity(const ExecutionIdentity& identity) {
  const auto identifier = [](std::string_view value) {
    const auto letter = [](char ch) {
      return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    };
    if (value.empty() || value.size() > 64 || !letter(value.front())) return false;
    for (const char ch : value)
      if (!letter(ch) && !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-') return false;
    return true;
  };
  if (!identifier(identity.graph_id) || !identifier(identity.instance_id) ||
      !valid_execution_id(identity.execution_id))
    throw std::invalid_argument("invalid graph/instance/execution identity");
}

std::string update_runtime_registration(const GraphConfig& config,
                                        const std::filesystem::path& manifest,
                                        std::string_view node_id, bool retire,
                                        std::string_view expected_execution) {
  static_cast<void>(config.node_for_instance(config.deployment.instance_id, node_id));
  if (retire && !valid_execution_id(expected_execution))
    throw std::invalid_argument("retire requires the current execution ID");
  const auto parent = std::filesystem::absolute(manifest).parent_path();
  struct stat metadata{};
  if (::lstat(parent.c_str(), &metadata) != 0 || !S_ISDIR(metadata.st_mode) ||
      metadata.st_uid != ::geteuid() || (metadata.st_mode & 0022) != 0)
    throw std::runtime_error("runtime identity directory must be owned and protected");
  auto lock = infra::detail::OwnershipLock::open_or_create(
      manifest.string() + ".lock", infra::detail::OwnershipLockMode::exclusive);
  const int input = ::open(manifest.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (input < 0) throw std::runtime_error("cannot open runtime identity manifest");
  std::string bytes;
  try {
    if (::fstat(input, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1 ||
        metadata.st_uid != ::geteuid() || (metadata.st_mode & 0022) != 0 || metadata.st_size < 0 ||
        metadata.st_size > 64 * 1024)
      throw std::runtime_error("runtime identity manifest must be bounded, owned and protected");
    bytes.resize(static_cast<std::size_t>(metadata.st_size));
    std::size_t offset{};
    while (offset < bytes.size()) {
      const auto count = ::read(input, bytes.data() + offset, bytes.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) throw std::runtime_error("cannot read runtime identity manifest");
      offset += static_cast<std::size_t>(count);
    }
  } catch (...) {
    ::close(input);
    throw;
  }
  ::close(input);
  auto document = YAML::Load(bytes);
  if (!document.IsMap() || document["version"].as<int>(0) != 2 ||
      document["graph_id"].as<std::string>("") != config.id ||
      document["instance_id"].as<std::string>("") != config.deployment.instance_id ||
      !document["nodes"].IsSequence() || document["nodes"].size() != config.nodes.size())
    throw std::runtime_error("runtime identity manifest scope or nodes mismatch");
  std::unordered_set<std::string> keys;
  for (const auto& field : document) {
    const auto key = field.first.as<std::string>();
    if (!keys.insert(key).second ||
        (key != "version" && key != "graph_id" && key != "instance_id" && key != "nodes"))
      throw std::runtime_error("unknown or duplicate runtime identity property");
  }
  std::unordered_set<std::string> nodes;
  YAML::Node selected;
  for (auto entry : document["nodes"]) {
    const auto id = entry["id"].as<std::string>("");
    static_cast<void>(config.node(id));
    const auto secret_file = entry["secret_file"].as<std::string>("");
    if (!nodes.insert(id).second || secret_file.empty() || secret_file.size() > 1024 ||
        secret_file.find('\0') != std::string::npos)
      throw std::runtime_error("invalid runtime identity node");
    keys.clear();
    for (const auto& field : entry) {
      const auto key = field.first.as<std::string>();
      if (!keys.insert(key).second ||
          (key != "id" && key != "secret_file" && key != "execution_id"))
        throw std::runtime_error("unknown or duplicate runtime identity node property");
    }
    if (entry["execution_id"] && !valid_execution_id(entry["execution_id"].as<std::string>()))
      throw std::runtime_error("invalid registered execution ID");
    if (id == node_id) selected.reset(entry);
  }
  std::string execution;
  if (retire) {
    if (selected["execution_id"].as<std::string>("") != expected_execution)
      throw std::runtime_error("execution identity changed; refusing retirement");
    selected.remove("execution_id");
  } else {
    if (selected["execution_id"])
      throw std::runtime_error("node already has an active execution; retire it first");
    do {
      execution = infra::detail::random_token();
    } while (!valid_execution_id(execution));
    selected["execution_id"] = execution;
  }
  YAML::Emitter emitter;
  emitter.SetMapFormat(YAML::Flow);
  emitter.SetSeqFormat(YAML::Flow);
  emitter.SetStringFormat(YAML::DoubleQuoted);
  emitter << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 2 << YAML::Key << "graph_id"
          << YAML::Value << config.id << YAML::Key << "instance_id" << YAML::Value
          << config.deployment.instance_id << YAML::Key << "nodes" << YAML::Value
          << document["nodes"] << YAML::EndMap;
  if (!emitter.good() || emitter.size() > 64 * 1024)
    throw std::runtime_error("cannot serialize bounded runtime identity manifest");
  const auto temporary = manifest.string() + "." + infra::detail::random_token() + ".tmp";
  const int output = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (output < 0) throw std::runtime_error("cannot publish runtime identity manifest");
  bool closed{};
  try {
    std::size_t offset{};
    while (offset < emitter.size()) {
      const auto count = ::write(output, emitter.c_str() + offset, emitter.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) throw std::runtime_error("cannot write runtime identity manifest");
      offset += static_cast<std::size_t>(count);
    }
    if (::fsync(output) != 0) throw std::runtime_error("cannot sync runtime identity manifest");
    ::close(output);
    closed = true;
    struct stat current{};
    if (::lstat(manifest.c_str(), &current) != 0 || current.st_dev != metadata.st_dev ||
        current.st_ino != metadata.st_ino || ::rename(temporary.c_str(), manifest.c_str()) != 0)
      throw std::runtime_error("runtime identity manifest changed during publication");
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) throw std::runtime_error("cannot open runtime identity directory");
    const auto synced = ::fsync(directory);
    ::close(directory);
    if (synced != 0) throw std::runtime_error("cannot sync runtime identity directory");
  } catch (...) {
    if (!closed) ::close(output);
    ::unlink(temporary.c_str());
    throw;
  }
  return execution;
}
}  // namespace graphx
