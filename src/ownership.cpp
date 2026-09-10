#include "graphx/ownership.hpp"

#include <yaml-cpp/yaml.h>

#include <openssl/rand.h>
#include <openssl/evp.h>

#include <array>
#include <algorithm>
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
#include <unordered_map>

namespace graphx {
namespace {

struct ExpectedEndpoint {
  AttachmentKind kind{AttachmentKind::container_veth};
  std::string id;
  std::string owner;
  std::string host_interface;
  std::string target_interface;
  std::string network_switch;
  std::string address;
  std::string mac;
  std::uint32_t mtu{1500};
  std::vector<RouteDefinition> routes;
  std::string container_id;
  std::uint64_t namespace_inode{};
  std::string namespace_name;
  std::uint32_t tap_uid{};
  std::uint32_t tap_gid{};
  VlanMetadata vlan;
};

struct OwnershipState {
  std::string phase{"M3"};
  std::string graph_id;
  std::string config_hash;
  std::string owner_token;
  std::string status;
  std::vector<std::string> expected_bridges;
  std::vector<OwnedResourceIdentity> bridges;
  std::vector<ExpectedEndpoint> expected_endpoints;
  std::vector<OwnedResourceIdentity> endpoints;
  std::vector<std::string> expected_namespaces;
  std::vector<OwnedResourceIdentity> namespaces;
};

OwnedResourceIdentity bridge_identity(std::string name, std::string uuid,
                                      std::string internal_port_uuid = {}) {
  OwnedResourceIdentity identity;
  identity.kind = "ovs_bridge";
  identity.name = std::move(name);
  identity.stable_id = std::move(uuid);
  identity.secondary_id = std::move(internal_port_uuid);
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

std::string ovs_get(const std::string& table, const std::string& record,
                    const std::string& column) {
  std::string value;
  if (run({"ovs-vsctl", "get", table, record, column}, &value) != 0) return {};
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.substr(1, value.size() - 2);
  return value;
}

std::vector<std::string> lines(std::string value) {
  std::vector<std::string> result;
  std::istringstream input(value);
  for (std::string line; std::getline(input, line);)
    if (!line.empty()) result.push_back(std::move(line));
  return result;
}

std::string ovs_find_uuid(const std::string& table, const std::string& column,
                          const std::string& value) {
  std::string found;
  if (run({"ovs-vsctl", "--data=bare", "--no-heading", "--columns=_uuid", "find", table,
           column + "=" + value},
          &found) != 0)
    return {};
  const auto matches = lines(found);
  return matches.size() == 1 ? matches.front() : std::string{};
}

struct ResolvedContainer {
  std::string id;
  std::uint32_t pid{};
  std::uint64_t namespace_inode{};
};

ResolvedContainer resolve_container(const GraphConfig& config, std::string_view owner) {
  const auto service =
      std::find_if(config.deployment.services.begin(), config.deployment.services.end(),
                   [&](const auto& candidate) { return candidate.node_id == owner; });
  if (service == config.deployment.services.end())
    throw std::runtime_error("no deployment service for container attachment owner " +
                             std::string(owner));
  std::string found;
  const auto project_label = "label=com.docker.compose.project=" + config.deployment.project;
  const auto service_label = "label=com.docker.compose.service=" + std::string(owner);
  if (run({"docker", "ps", "--filter", project_label, "--filter", service_label, "--filter",
           "status=running", "--format", "{{.ID}}"},
          &found) != 0)
    throw std::runtime_error("cannot resolve Docker deployment identity for " + std::string(owner));
  const auto matches = lines(found);
  if (matches.size() != 1)
    throw std::runtime_error("expected exactly one running container for Compose service " +
                             std::string(owner));
  std::string inspected;
  const std::string format =
      "{{.Id}}\n{{.State.Running}}\n{{.State.Pid}}\n{{.Config.Image}}\n"
      "{{index .Config.Labels \"com.docker.compose.project\"}}\n"
      "{{index .Config.Labels \"com.docker.compose.service\"}}";
  if (run({"docker", "inspect", "--format", format, matches.front()}, &inspected) != 0)
    throw std::runtime_error("cannot inspect Docker deployment identity for " + std::string(owner));
  const auto fields = lines(inspected);
  if (fields.size() != 6 || fields[1] != "true" || fields[3] != service->image ||
      fields[4] != config.deployment.project || fields[5] != owner)
    throw std::runtime_error("Docker deployment identity does not match declared service " +
                             std::string(owner));
  if (fields[0].size() != 64 || !std::all_of(fields[0].begin(), fields[0].end(), [](char value) {
        return std::isxdigit(static_cast<unsigned char>(value));
      }))
    throw std::runtime_error("Docker returned an invalid full container identity");
  std::size_t consumed{};
  const auto parsed_pid = std::stoul(fields[2], &consumed);
  if (consumed != fields[2].size() || parsed_pid == 0 || parsed_pid > UINT32_MAX)
    throw std::runtime_error("Docker returned an invalid container PID");
  struct stat metadata{};
  const auto namespace_path = "/proc/" + fields[2] + "/ns/net";
  if (::stat(namespace_path.c_str(), &metadata) != 0)
    throw std::runtime_error("cannot inspect container network namespace for " +
                             std::string(owner));
  return {fields[0], static_cast<std::uint32_t>(parsed_pid),
          static_cast<std::uint64_t>(metadata.st_ino)};
}

std::optional<std::uint32_t> link_ifindex(const std::string& name) {
  std::ifstream input("/sys/class/net/" + name + "/ifindex");
  std::uint32_t value{};
  if (!(input >> value)) return std::nullopt;
  return value;
}

std::string link_alias(const std::string& name) {
  std::ifstream input("/sys/class/net/" + name + "/ifalias");
  std::string value;
  std::getline(input, value);
  return value;
}

std::optional<std::uint64_t> namespace_inode(const std::string& name) {
  std::string value;
  if (run({"ip", "netns", "exec", name, "stat", "-Lc", "%i", "/proc/self/ns/net"}, &value) != 0)
    return std::nullopt;
  try {
    std::size_t consumed{};
    const auto inode = std::stoull(value, &consumed);
    if (consumed != value.size() || inode == 0) return std::nullopt;
    return inode;
  } catch (...) {
    return std::nullopt;
  }
}

std::string namespace_alias(const OwnershipState& state, std::string_view name) {
  return "graphx:" + state.owner_token + ":netns:" + std::string(name);
}

bool namespace_owned(const OwnedResourceIdentity& item, const OwnershipState& state) {
  const auto inode = namespace_inode(item.name);
  if (!inode || !item.namespace_inode || *inode != *item.namespace_inode) return false;
  std::string loopback;
  return run({"ip", "netns", "exec", item.name, "ip", "-d", "link", "show", "dev", "lo"},
             &loopback) == 0 &&
         loopback.find(namespace_alias(state, item.name)) != std::string::npos;
}

OwnedResourceIdentity create_namespace(const RouterDefinition& router,
                                       const OwnershipState& state) {
  if (namespace_inode(router.namespace_name))
    throw std::runtime_error("refusing unowned Linux namespace collision: " +
                             router.namespace_name);
  if (run({"ip", "netns", "add", router.namespace_name}) != 0 ||
      run({"ip", "netns", "exec", router.namespace_name, "ip", "link", "set", "dev", "lo", "alias",
           namespace_alias(state, router.namespace_name)}) != 0 ||
      run({"ip", "netns", "exec", router.namespace_name, "ip", "link", "set", "dev", "lo", "up"}) !=
          0)
    throw std::runtime_error("cannot create owned Linux namespace " + router.namespace_name);
  const auto inode = namespace_inode(router.namespace_name);
  if (!inode) throw std::runtime_error("cannot capture Linux namespace identity");
  OwnedResourceIdentity identity;
  identity.kind = "linux_namespace";
  identity.name = router.namespace_name;
  identity.namespace_inode = *inode;
  return identity;
}

bool delete_owned_namespace(const OwnedResourceIdentity& item, const OwnershipState& state) {
  if (!namespace_inode(item.name)) return true;
  return namespace_owned(item, state) && run({"ip", "netns", "delete", item.name}) == 0;
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
  root["phase"] = state.phase;
  root["graph_id"] = state.graph_id;
  root["config_sha256"] = state.config_hash;
  root["owner_token"] = state.owner_token;
  root["status"] = state.status;
  for (const auto& name : state.expected_bridges) root["expected_bridges"].push_back(name);
  for (const auto& name : state.expected_namespaces) root["expected_namespaces"].push_back(name);
  for (const auto& endpoint : state.expected_endpoints) {
    YAML::Node item;
    item["id"] = endpoint.id;
    item["kind"] = std::string(to_string(endpoint.kind));
    item["owner"] = endpoint.owner;
    item["host_interface"] = endpoint.host_interface;
    item["target_interface"] = endpoint.target_interface;
    item["switch"] = endpoint.network_switch;
    item["address"] = endpoint.address;
    if (!endpoint.mac.empty()) item["mac"] = endpoint.mac;
    item["mtu"] = endpoint.mtu;
    if (!endpoint.container_id.empty()) item["container_id"] = endpoint.container_id;
    if (!endpoint.namespace_name.empty()) item["namespace_name"] = endpoint.namespace_name;
    item["namespace_inode"] = endpoint.namespace_inode;
    if (endpoint.tap_uid != 0) item["tap_uid"] = endpoint.tap_uid;
    if (endpoint.tap_gid != 0) item["tap_gid"] = endpoint.tap_gid;
    if (endpoint.vlan.access_tag) item["access_tag"] = *endpoint.vlan.access_tag;
    for (const auto trunk : endpoint.vlan.trunks) item["trunks"].push_back(trunk);
    for (const auto& route : endpoint.routes) {
      YAML::Node route_node;
      route_node["destination"] = route.destination;
      if (!route.via.empty()) route_node["via"] = route.via;
      item["routes"].push_back(route_node);
    }
    root["expected_endpoints"].push_back(item);
  }
  for (const auto& bridge : state.bridges) {
    YAML::Node item;
    item["kind"] = bridge.kind;
    item["name"] = bridge.name;
    item["uuid"] = bridge.stable_id;
    if (!bridge.secondary_id.empty()) item["internal_port_uuid"] = bridge.secondary_id;
    root["resources"].push_back(item);
  }
  for (const auto& endpoint : state.endpoints) {
    YAML::Node item;
    item["kind"] = endpoint.kind;
    item["attachment_id"] = endpoint.attachment_id;
    item["name"] = endpoint.name;
    item["target_interface"] = endpoint.target_interface;
    item["port_uuid"] = endpoint.stable_id;
    item["interface_uuid"] = endpoint.secondary_id;
    item["ifindex"] = *endpoint.ifindex;
    item["peer_ifindex"] = *endpoint.peer_ifindex;
    item["namespace_inode"] = *endpoint.namespace_inode;
    if (!endpoint.container_id.empty()) item["container_id"] = endpoint.container_id;
    if (!endpoint.tap_owner.empty()) item["tap_owner"] = endpoint.tap_owner;
    if (!endpoint.route_identity.empty()) item["mirror_uuid"] = endpoint.route_identity;
    root["resources"].push_back(item);
  }
  for (const auto& network_namespace : state.namespaces) {
    YAML::Node item;
    item["kind"] = network_namespace.kind;
    item["name"] = network_namespace.name;
    item["namespace_inode"] = *network_namespace.namespace_inode;
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
  if (!root.IsMap() || root["version"].as<int>(0) != 1 ||
      (required_scalar(root, "phase") != "M3" && required_scalar(root, "phase") != "M4" &&
       required_scalar(root, "phase") != "M5" && required_scalar(root, "phase") != "M6"))
    throw std::runtime_error("unsupported ownership state format");
  OwnershipState state;
  state.phase = required_scalar(root, "phase");
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
  if (root["expected_namespaces"])
    for (const auto& value : root["expected_namespaces"]) {
      const auto name = value.as<std::string>();
      if (name.empty()) throw std::runtime_error("invalid expected Linux namespace");
      state.expected_namespaces.push_back(name);
    }
  std::unordered_set<std::string> expected_endpoint_ids;
  if (root["expected_endpoints"])
    for (const auto& value : root["expected_endpoints"]) {
      ExpectedEndpoint endpoint;
      endpoint.id = required_scalar(value, "id");
      if (value["kind"]) {
        const auto kind = value["kind"].as<std::string>();
        if (kind == "namespace_veth")
          endpoint.kind = AttachmentKind::namespace_veth;
        else if (kind == "qemu_tap")
          endpoint.kind = AttachmentKind::qemu_tap;
        else if (kind == "mirror")
          endpoint.kind = AttachmentKind::mirror;
        else if (kind != "container_veth")
          throw std::runtime_error("invalid endpoint kind");
      }
      endpoint.owner = required_scalar(value, "owner");
      endpoint.host_interface = required_scalar(value, "host_interface");
      endpoint.target_interface = required_scalar(value, "target_interface");
      endpoint.network_switch = required_scalar(value, "switch");
      if (value["address"]) endpoint.address = value["address"].as<std::string>();
      if (value["mac"]) endpoint.mac = value["mac"].as<std::string>();
      endpoint.mtu = value["mtu"].as<std::uint32_t>(1500);
      if (value["container_id"]) endpoint.container_id = value["container_id"].as<std::string>();
      if (value["namespace_name"])
        endpoint.namespace_name = value["namespace_name"].as<std::string>();
      endpoint.namespace_inode = value["namespace_inode"].as<std::uint64_t>(0);
      endpoint.tap_uid = value["tap_uid"].as<std::uint32_t>(0);
      endpoint.tap_gid = value["tap_gid"].as<std::uint32_t>(0);
      if (value["access_tag"]) endpoint.vlan.access_tag = value["access_tag"].as<std::uint16_t>();
      if (value["trunks"])
        for (const auto& trunk : value["trunks"])
          endpoint.vlan.trunks.push_back(trunk.as<std::uint16_t>());
      if (!expected_endpoint_ids.insert(endpoint.id).second || endpoint.namespace_inode == 0 ||
          (endpoint.kind == AttachmentKind::container_veth && endpoint.container_id.empty()) ||
          (endpoint.kind == AttachmentKind::namespace_veth && endpoint.namespace_name.empty()) ||
          (endpoint.kind == AttachmentKind::qemu_tap &&
           (endpoint.tap_uid == 0 || endpoint.tap_gid == 0)))
        throw std::runtime_error("invalid expected endpoint");
      if (value["routes"])
        for (const auto& route_value : value["routes"]) {
          RouteDefinition route;
          route.destination = required_scalar(route_value, "destination");
          if (route_value["via"]) route.via = route_value["via"].as<std::string>();
          endpoint.routes.push_back(std::move(route));
        }
      state.expected_endpoints.push_back(std::move(endpoint));
    }
  std::unordered_set<std::string> recorded;
  if (root["resources"])
    for (const auto& value : root["resources"]) {
      const auto kind = required_scalar(value, "kind");
      if (kind == "container_veth" || kind == "namespace_veth" || kind == "qemu_tap" ||
          kind == "mirror_veth") {
        OwnedResourceIdentity endpoint;
        endpoint.kind = kind;
        endpoint.attachment_id = required_scalar(value, "attachment_id");
        endpoint.name = required_scalar(value, "name");
        endpoint.target_interface = required_scalar(value, "target_interface");
        endpoint.stable_id = required_scalar(value, "port_uuid");
        endpoint.secondary_id = required_scalar(value, "interface_uuid");
        endpoint.ifindex = value["ifindex"].as<std::uint32_t>(0);
        endpoint.peer_ifindex = value["peer_ifindex"].as<std::uint32_t>(0);
        endpoint.namespace_inode = value["namespace_inode"].as<std::uint64_t>(0);
        if (value["container_id"]) endpoint.container_id = value["container_id"].as<std::string>();
        if (value["tap_owner"]) endpoint.tap_owner = value["tap_owner"].as<std::string>();
        if (value["mirror_uuid"]) endpoint.route_identity = value["mirror_uuid"].as<std::string>();
        if (!expected_endpoint_ids.contains(endpoint.attachment_id) || !*endpoint.ifindex ||
            !*endpoint.peer_ifindex || !*endpoint.namespace_inode ||
            (kind == "qemu_tap" && endpoint.tap_owner.empty()))
          throw std::runtime_error("invalid owned endpoint");
        state.endpoints.push_back(std::move(endpoint));
        continue;
      }
      if (kind == "linux_namespace") {
        OwnedResourceIdentity item;
        item.kind = kind;
        item.name = required_scalar(value, "name");
        item.namespace_inode = value["namespace_inode"].as<std::uint64_t>(0);
        if (!*item.namespace_inode) throw std::runtime_error("invalid owned Linux namespace");
        state.namespaces.push_back(std::move(item));
        continue;
      }
      if (kind != "ovs_bridge") throw std::runtime_error("unsupported owned resource kind");
      const auto name = required_scalar(value, "name");
      const auto uuid = required_scalar(value, "uuid");
      const auto internal_port_uuid = value["internal_port_uuid"]
                                          ? value["internal_port_uuid"].as<std::string>()
                                          : std::string{};
      if (!expected.contains(name) || !recorded.insert(name).second || uuid.size() != 36 ||
          (!internal_port_uuid.empty() && internal_port_uuid.size() != 36))
        throw std::runtime_error("invalid owned bridge record");
      state.bridges.push_back(bridge_identity(name, uuid, internal_port_uuid));
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
  const auto internal_port =
      bridge.secondary_id.empty() ? ovs_get("Port", bridge.name, "_uuid") : bridge.secondary_id;
  return !internal_port.empty() && bridge_exists(bridge.name) &&
         ovs_get(bridge.name, "_uuid") == bridge.stable_id &&
         ovs_get("Port", bridge.name, "_uuid") == internal_port &&
         ovs_get(bridge.name, "external_ids:graphx_owner") == state.owner_token &&
         ovs_get(bridge.name, "external_ids:graphx_config_hash") == state.config_hash &&
         ovs_get(bridge.name, "external_ids:graphx_graph") == state.graph_id;
}

std::unordered_set<std::string> ovs_uuid_set(std::string value) {
  std::unordered_set<std::string> result;
  std::string token;
  const auto flush = [&] {
    if (!token.empty()) result.insert(std::exchange(token, {}));
  };
  for (const auto character : value) {
    if (std::isxdigit(static_cast<unsigned char>(character)) || character == '-')
      token.push_back(character);
    else
      flush();
  }
  flush();
  return result;
}

bool bridge_complete_set_owned(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  if (!bridge_owned(bridge, state)) return false;
  const auto internal_port =
      bridge.secondary_id.empty() ? ovs_get("Port", bridge.name, "_uuid") : bridge.secondary_id;
  std::unordered_set<std::string> expected{internal_port};
  for (const auto& endpoint : state.endpoints) {
    const auto definition =
        std::find_if(state.expected_endpoints.begin(), state.expected_endpoints.end(),
                     [&](const auto& candidate) { return candidate.id == endpoint.attachment_id; });
    if (definition == state.expected_endpoints.end()) return false;
    if (definition->network_switch == bridge.name &&
        ovs_get("Port", endpoint.stable_id, "_uuid") == endpoint.stable_id)
      expected.insert(endpoint.stable_id);
  }
  return ovs_uuid_set(ovs_get("Bridge", bridge.stable_id, "ports")) == expected;
}

bool delete_owned_bridge(const OwnedResourceIdentity& bridge, const OwnershipState& state) {
  const auto internal_port =
      bridge.secondary_id.empty() ? ovs_get("Port", bridge.name, "_uuid") : bridge.secondary_id;
  if (internal_port.empty() || ovs_get("Port", bridge.name, "_uuid") != internal_port ||
      ovs_get("Bridge", bridge.stable_id, "ports") != "[" + internal_port + "]")
    return false;
  return run({"ovs-vsctl",
              "--timeout=2",
              "--",
              "wait-until",
              "Bridge",
              bridge.stable_id,
              "external_ids:graphx_owner=" + state.owner_token,
              "external_ids:graphx_config_hash=" + state.config_hash,
              "external_ids:graphx_graph=" + state.graph_id,
              "ports=[" + internal_port + "]",
              "--",
              "remove",
              "Open_vSwitch",
              ".",
              "bridges",
              bridge.stable_id,
              "--",
              "destroy",
              "Bridge",
              bridge.stable_id}) == 0;
}

std::string endpoint_alias(const OwnershipState& state, std::string_view attachment,
                           std::string_view side) {
  return "graphx:" + state.owner_token + ":" + std::string(attachment) + ":" + std::string(side);
}

const ExpectedEndpoint& expected_endpoint(const OwnershipState& state, std::string_view id) {
  const auto found = std::find_if(state.expected_endpoints.begin(), state.expected_endpoints.end(),
                                  [&](const auto& candidate) { return candidate.id == id; });
  if (found == state.expected_endpoints.end())
    throw std::runtime_error("missing expected endpoint record " + std::string(id));
  return *found;
}

bool ovs_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  return ovs_get("Port", endpoint.stable_id, "_uuid") == endpoint.stable_id &&
         ovs_get("Interface", endpoint.secondary_id, "_uuid") == endpoint.secondary_id &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_owner") == state.owner_token &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_attachment") ==
             endpoint.attachment_id &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_config_hash") ==
             state.config_hash &&
         ovs_get("Port", endpoint.stable_id, "external_ids:graphx_graph") == state.graph_id &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_owner") ==
             state.owner_token &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_attachment") ==
             endpoint.attachment_id &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_config_hash") ==
             state.config_hash &&
         ovs_get("Interface", endpoint.secondary_id, "external_ids:graphx_graph") == state.graph_id;
}

bool host_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  const auto observed = link_ifindex(endpoint.name);
  return observed && endpoint.ifindex && *observed == *endpoint.ifindex &&
         link_alias(endpoint.name) == endpoint_alias(state, endpoint.attachment_id, "host");
}

std::string tap_owner_identity(std::uint32_t uid, std::uint32_t gid) {
  return std::to_string(uid) + ":" + std::to_string(gid);
}

bool tap_owner_matches(const ExpectedEndpoint& expected) {
  std::string details;
  if (run({"ip", "tuntap", "show", "dev", expected.host_interface}, &details) != 0) return false;
  return details.find(" tap ") != std::string::npos &&
         details.find(" user " + std::to_string(expected.tap_uid)) != std::string::npos &&
         details.find(" group " + std::to_string(expected.tap_gid)) != std::string::npos;
}

std::string comma_join(const std::vector<std::uint16_t>& values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) output << ',';
    output << values[index];
  }
  return output.str();
}

void configure_endpoint_vlan(const ExpectedEndpoint& endpoint) {
  if (endpoint.vlan.access_tag && run({"ovs-vsctl", "set", "Port", endpoint.host_interface,
                                       "tag=" + std::to_string(*endpoint.vlan.access_tag)}) != 0)
    throw std::runtime_error("cannot configure OVS access VLAN for " + endpoint.id);
  if (!endpoint.vlan.trunks.empty() && run({"ovs-vsctl", "set", "Port", endpoint.host_interface,
                                            "trunks=" + comma_join(endpoint.vlan.trunks)}) != 0)
    throw std::runtime_error("cannot configure OVS trunk VLANs for " + endpoint.id);
}

bool endpoint_vlan_matches(const ExpectedEndpoint& expected,
                           const OwnedResourceIdentity& endpoint) {
  if (expected.vlan.access_tag &&
      ovs_get("Port", endpoint.stable_id, "tag") != std::to_string(*expected.vlan.access_tag))
    return false;
  if (!expected.vlan.trunks.empty()) {
    const auto observed = ovs_uuid_set(ovs_get("Port", endpoint.stable_id, "trunks"));
    std::unordered_set<std::string> wanted;
    for (const auto value : expected.vlan.trunks) wanted.insert(std::to_string(value));
    if (observed != wanted) return false;
  }
  return true;
}

bool endpoint_names_absent_or_recorded(const OwnedResourceIdentity& endpoint) {
  const auto port_uuid = ovs_get("Port", endpoint.name, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.name, "_uuid");
  return (port_uuid.empty() || port_uuid == endpoint.stable_id) &&
         (interface_uuid.empty() || interface_uuid == endpoint.secondary_id);
}

bool delete_owned_endpoint(const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  if (!endpoint_names_absent_or_recorded(endpoint)) return false;
  if (!endpoint.route_identity.empty()) {
    if (ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_owner") !=
            state.owner_token ||
        ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_attachment") !=
            endpoint.attachment_id)
      return false;
    if (run({"ovs-vsctl", "--", "remove", "Bridge",
             expected_endpoint(state, endpoint.attachment_id).network_switch, "mirrors",
             endpoint.route_identity, "--", "destroy", "Mirror", endpoint.route_identity}) != 0)
      return false;
  }
  if (ovs_get("Port", endpoint.stable_id, "_uuid") == endpoint.stable_id) {
    if (!ovs_endpoint_owned(endpoint, state)) return false;
    const auto& expected = expected_endpoint(state, endpoint.attachment_id);
    const auto bridge = std::find_if(
        state.bridges.begin(), state.bridges.end(),
        [&](const auto& candidate) { return candidate.name == expected.network_switch; });
    if (bridge == state.bridges.end()) return false;
    if (run({"ovs-vsctl",
             "--timeout=2",
             "--",
             "wait-until",
             "Port",
             endpoint.stable_id,
             "external_ids:graphx_owner=" + state.owner_token,
             "external_ids:graphx_attachment=" + endpoint.attachment_id,
             "external_ids:graphx_config_hash=" + state.config_hash,
             "external_ids:graphx_graph=" + state.graph_id,
             "--",
             "remove",
             "Bridge",
             bridge->stable_id,
             "ports",
             endpoint.stable_id,
             "--",
             "destroy",
             "Interface",
             endpoint.secondary_id,
             "--",
             "destroy",
             "Port",
             endpoint.stable_id}) != 0)
      return false;
  }
  if (link_ifindex(endpoint.name)) {
    if (!host_endpoint_owned(endpoint, state)) return false;
    const auto& expected = expected_endpoint(state, endpoint.attachment_id);
    if (expected.kind == AttachmentKind::qemu_tap) {
      if (!tap_owner_matches(expected) ||
          endpoint.tap_owner != tap_owner_identity(expected.tap_uid, expected.tap_gid) ||
          run({"ip", "tuntap", "delete", "dev", endpoint.name, "mode", "tap"}) != 0)
        return false;
    } else if (run({"ip", "link", "delete", "dev", endpoint.name}) != 0) {
      return false;
    }
  }
  return true;
}

bool tap_endpoint_healthy(const ExpectedEndpoint& expected, const OwnedResourceIdentity& endpoint,
                          const OwnershipState& state) {
  std::string link;
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state) &&
         endpoint_vlan_matches(expected, endpoint) && tap_owner_matches(expected) &&
         endpoint.tap_owner == tap_owner_identity(expected.tap_uid, expected.tap_gid) &&
         run({"ip", "-d", "-o", "link", "show", "dev", expected.host_interface}, &link) == 0 &&
         link.find("mtu " + std::to_string(expected.mtu)) != std::string::npos &&
         link.find("UP") != std::string::npos;
}

bool container_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint, const OwnershipState& state,
                                const GraphConfig& config) {
  const auto container = resolve_container(config, expected.owner);
  if (container.id != endpoint.container_id ||
      container.namespace_inode != *endpoint.namespace_inode)
    return false;
  std::string link;
  if (run({"nsenter", "-t", std::to_string(container.pid), "-n", "--", "ip", "-d", "-o", "link",
           "show", "dev", expected.target_interface},
          &link) != 0)
    return false;
  const auto colon = link.find(':');
  if (colon == std::string::npos || !endpoint.peer_ifindex ||
      std::stoul(link.substr(0, colon)) != *endpoint.peer_ifindex)
    return false;
  if (link.find("mtu " + std::to_string(expected.mtu)) == std::string::npos ||
      link.find(endpoint_alias(state, endpoint.attachment_id, "peer")) == std::string::npos ||
      (!expected.mac.empty() && link.find(expected.mac) == std::string::npos))
    return false;
  std::string address;
  if (run({"nsenter", "-t", std::to_string(container.pid), "-n", "--", "ip", "-o", "-4", "address",
           "show", "dev", expected.target_interface},
          &address) != 0 ||
      address.find(expected.address) == std::string::npos)
    return false;
  for (const auto& route : expected.routes) {
    std::string observed;
    if (run({"nsenter", "-t", std::to_string(container.pid), "-n", "--", "ip", "-4", "route",
             "show", route.destination},
            &observed) != 0 ||
        observed.find(route.destination) == std::string::npos ||
        observed.find("dev " + expected.target_interface) == std::string::npos ||
        (!route.via.empty() && observed.find("via " + route.via) == std::string::npos))
      return false;
  }
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state);
}

bool namespace_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint,
                                const OwnershipState& state) {
  const auto inode = namespace_inode(expected.namespace_name);
  if (!inode || !endpoint.namespace_inode || *inode != *endpoint.namespace_inode) return false;
  std::string link;
  if (run({"ip", "netns", "exec", expected.namespace_name, "ip", "-d", "-o", "link", "show", "dev",
           expected.target_interface},
          &link) != 0 ||
      link.find(endpoint_alias(state, endpoint.attachment_id, "peer")) == std::string::npos)
    return false;
  std::string address;
  if (run({"ip", "netns", "exec", expected.namespace_name, "ip", "-o", "-4", "address", "show",
           "dev", expected.target_interface},
          &address) != 0 ||
      address.find(expected.address) == std::string::npos)
    return false;
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state);
}

bool mirror_endpoint_healthy(const ExpectedEndpoint& expected,
                             const OwnedResourceIdentity& endpoint, const OwnershipState& state) {
  return host_endpoint_owned(endpoint, state) && ovs_endpoint_owned(endpoint, state) &&
         link_ifindex(expected.target_interface) == endpoint.peer_ifindex &&
         link_alias(expected.target_interface) ==
             endpoint_alias(state, endpoint.attachment_id, "peer") &&
         !endpoint.route_identity.empty() &&
         ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_owner") ==
             state.owner_token &&
         ovs_get("Mirror", endpoint.route_identity, "external_ids:graphx_attachment") ==
             endpoint.attachment_id;
}

void check_endpoint_collision(const ExpectedEndpoint& endpoint, std::uint32_t pid) {
  if (link_ifindex(endpoint.host_interface))
    throw std::runtime_error("refusing unowned host interface collision: " +
                             endpoint.host_interface);
  if (!ovs_get("Port", endpoint.host_interface, "_uuid").empty() ||
      !ovs_get("Interface", endpoint.host_interface, "_uuid").empty())
    throw std::runtime_error("refusing unowned OVS endpoint collision: " + endpoint.host_interface);
  if (run({"nsenter", "-t", std::to_string(pid), "-n", "--", "ip", "link", "show", "dev",
           endpoint.target_interface}) == 0)
    throw std::runtime_error("refusing container interface collision: " +
                             endpoint.target_interface);
}

OwnedResourceIdentity create_endpoint(const ExpectedEndpoint& endpoint, std::uint32_t pid,
                                      const OwnershipState& state) {
  if (run({"ip", "link", "add", endpoint.host_interface, "type", "veth", "peer", "name",
           endpoint.target_interface}) != 0)
    throw std::runtime_error("cannot create veth for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "alias",
           endpoint_alias(state, endpoint.id, "peer")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.host_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0)
    throw std::runtime_error("cannot mark veth ownership for attachment " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  const auto peer_ifindex = link_ifindex(endpoint.target_interface);
  if (!ifindex || !peer_ifindex)
    throw std::runtime_error("cannot capture veth ifindices for attachment " + endpoint.id);
  const std::vector<std::string> ovs_command = {
      "ovs-vsctl",
      "--",
      "--id=@i",
      "create",
      "Interface",
      "name=" + endpoint.host_interface,
      "external_ids:graphx_owner=" + state.owner_token,
      "external_ids:graphx_attachment=" + endpoint.id,
      "external_ids:graphx_graph=" + state.graph_id,
      "external_ids:graphx_config_hash=" + state.config_hash,
      "--",
      "--id=@p",
      "create",
      "Port",
      "name=" + endpoint.host_interface,
      "interfaces=@i",
      "external_ids:graphx_owner=" + state.owner_token,
      "external_ids:graphx_attachment=" + endpoint.id,
      "external_ids:graphx_graph=" + state.graph_id,
      "external_ids:graphx_config_hash=" + state.config_hash,
      "--",
      "add",
      "Bridge",
      endpoint.network_switch,
      "ports",
      "@p"};
  if (run(ovs_command) != 0)
    throw std::runtime_error("cannot attach owned veth to OVS for attachment " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (port_uuid.empty() || interface_uuid.empty())
    throw std::runtime_error("cannot capture OVS endpoint identity for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.target_interface, "netns", std::to_string(pid)}) !=
      0)
    throw std::runtime_error("cannot move veth into container namespace for attachment " +
                             endpoint.id);
  const auto ns = [&](std::vector<std::string> command) {
    std::vector<std::string> prefix = {"nsenter", "-t", std::to_string(pid), "-n", "--"};
    prefix.insert(prefix.end(), command.begin(), command.end());
    if (run(prefix) != 0)
      throw std::runtime_error("cannot configure container endpoint " + endpoint.id);
  };
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "mtu", std::to_string(endpoint.mtu)});
  if (!endpoint.mac.empty())
    ns({"ip", "link", "set", "dev", endpoint.target_interface, "address", endpoint.mac});
  ns({"ip", "address", "replace", endpoint.address, "dev", endpoint.target_interface});
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "up"});
  for (const auto& route : endpoint.routes) {
    std::vector<std::string> command = {"ip", "route", "replace", route.destination};
    if (!route.via.empty()) command.insert(command.end(), {"via", route.via});
    command.insert(command.end(), {"dev", endpoint.target_interface});
    ns(std::move(command));
  }
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable host veth for attachment " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "container_veth";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.target_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *peer_ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  identity.container_id = endpoint.container_id;
  return identity;
}

OwnedResourceIdentity create_namespace_endpoint(const ExpectedEndpoint& endpoint,
                                                const OwnershipState& state) {
  if (run({"ip", "link", "add", endpoint.host_interface, "type", "veth", "peer", "name",
           endpoint.target_interface}) != 0)
    throw std::runtime_error("cannot create namespace veth for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "alias",
           endpoint_alias(state, endpoint.id, "peer")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.host_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0)
    throw std::runtime_error("cannot mark namespace veth " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  const auto peer_ifindex = link_ifindex(endpoint.target_interface);
  if (!ifindex || !peer_ifindex) throw std::runtime_error("cannot capture namespace veth identity");
  if (run({"ovs-vsctl",
           "--",
           "--id=@i",
           "create",
           "Interface",
           "name=" + endpoint.host_interface,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "--id=@p",
           "create",
           "Port",
           "name=" + endpoint.host_interface,
           "interfaces=@i",
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "add",
           "Bridge",
           endpoint.network_switch,
           "ports",
           "@p"}) != 0)
    throw std::runtime_error("cannot attach namespace veth to OVS " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (port_uuid.empty() || interface_uuid.empty())
    throw std::runtime_error("cannot capture namespace OVS identity");
  configure_endpoint_vlan(endpoint);
  if (run({"ip", "link", "set", "dev", endpoint.target_interface, "netns",
           endpoint.namespace_name}) != 0)
    throw std::runtime_error("cannot move veth into namespace " + endpoint.namespace_name);
  const auto ns = [&](std::vector<std::string> command) {
    std::vector<std::string> prefix = {"ip", "netns", "exec", endpoint.namespace_name};
    prefix.insert(prefix.end(), command.begin(), command.end());
    if (run(prefix) != 0)
      throw std::runtime_error("cannot configure namespace endpoint " + endpoint.id);
  };
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "mtu", std::to_string(endpoint.mtu)});
  if (!endpoint.mac.empty())
    ns({"ip", "link", "set", "dev", endpoint.target_interface, "address", endpoint.mac});
  ns({"ip", "address", "replace", endpoint.address, "dev", endpoint.target_interface});
  ns({"ip", "link", "set", "dev", endpoint.target_interface, "up"});
  for (const auto& route : endpoint.routes) {
    std::vector<std::string> command = {"ip", "route", "replace", route.destination};
    if (!route.via.empty()) command.insert(command.end(), {"via", route.via});
    command.insert(command.end(), {"dev", endpoint.target_interface});
    ns(std::move(command));
  }
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable namespace host veth " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "namespace_veth";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.target_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *peer_ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  return identity;
}

OwnedResourceIdentity create_tap_endpoint(const ExpectedEndpoint& endpoint,
                                          const OwnershipState& state) {
  const auto uid = std::to_string(endpoint.tap_uid);
  const auto gid = std::to_string(endpoint.tap_gid);
  if (run({"ip", "tuntap", "add", "dev", endpoint.host_interface, "mode", "tap", "user", uid,
           "group", gid}) != 0)
    throw std::runtime_error("cannot create TAP for attachment " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.host_interface, "mtu",
           std::to_string(endpoint.mtu)}) != 0)
    throw std::runtime_error("cannot mark TAP ownership for attachment " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  if (!ifindex)
    throw std::runtime_error("cannot capture TAP ifindex for attachment " + endpoint.id);
  if (run({"ovs-vsctl",
           "--",
           "--id=@i",
           "create",
           "Interface",
           "name=" + endpoint.host_interface,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "--id=@p",
           "create",
           "Port",
           "name=" + endpoint.host_interface,
           "interfaces=@i",
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "add",
           "Bridge",
           endpoint.network_switch,
           "ports",
           "@p"}) != 0)
    throw std::runtime_error("cannot attach owned TAP to OVS for attachment " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (port_uuid.empty() || interface_uuid.empty())
    throw std::runtime_error("cannot capture TAP OVS identity for attachment " + endpoint.id);
  configure_endpoint_vlan(endpoint);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable TAP for attachment " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "qemu_tap";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.host_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  identity.tap_owner = tap_owner_identity(endpoint.tap_uid, endpoint.tap_gid);
  return identity;
}

OwnedResourceIdentity create_mirror_endpoint(const ExpectedEndpoint& endpoint,
                                             const OwnershipState& state) {
  if (run({"ip", "link", "add", endpoint.host_interface, "type", "veth", "peer", "name",
           endpoint.target_interface}) != 0)
    throw std::runtime_error("cannot create mirror veth " + endpoint.id);
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "alias",
           endpoint_alias(state, endpoint.id, "host")}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "alias",
           endpoint_alias(state, endpoint.id, "peer")}) != 0)
    throw std::runtime_error("cannot mark mirror veth " + endpoint.id);
  const auto ifindex = link_ifindex(endpoint.host_interface);
  const auto peer_ifindex = link_ifindex(endpoint.target_interface);
  if (!ifindex || !peer_ifindex) throw std::runtime_error("cannot capture mirror veth identity");
  if (run({"ovs-vsctl",
           "--",
           "--id=@i",
           "create",
           "Interface",
           "name=" + endpoint.host_interface,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "--id=@p",
           "create",
           "Port",
           "name=" + endpoint.host_interface,
           "interfaces=@i",
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash,
           "--",
           "add",
           "Bridge",
           endpoint.network_switch,
           "ports",
           "@p"}) != 0)
    throw std::runtime_error("cannot attach mirror veth " + endpoint.id);
  const auto port_uuid = ovs_get("Port", endpoint.host_interface, "_uuid");
  const auto interface_uuid = ovs_get("Interface", endpoint.host_interface, "_uuid");
  if (run({"ovs-vsctl", "--", "--id=@m", "create", "Mirror", "name=" + endpoint.id,
           "select_all=true", "output-port=" + port_uuid,
           "external_ids:graphx_owner=" + state.owner_token,
           "external_ids:graphx_attachment=" + endpoint.id,
           "external_ids:graphx_graph=" + state.graph_id,
           "external_ids:graphx_config_hash=" + state.config_hash, "--", "add", "Bridge",
           endpoint.network_switch, "mirrors", "@m"}) != 0)
    throw std::runtime_error("cannot create OVS mirror " + endpoint.id);
  const auto mirror_uuid = ovs_get("Mirror", endpoint.id, "_uuid");
  if (mirror_uuid.empty()) throw std::runtime_error("cannot capture OVS mirror identity");
  if (run({"ip", "link", "set", "dev", endpoint.host_interface, "up"}) != 0 ||
      run({"ip", "link", "set", "dev", endpoint.target_interface, "up"}) != 0)
    throw std::runtime_error("cannot enable mirror veth " + endpoint.id);
  OwnedResourceIdentity identity;
  identity.kind = "mirror_veth";
  identity.name = endpoint.host_interface;
  identity.target_interface = endpoint.target_interface;
  identity.attachment_id = endpoint.id;
  identity.stable_id = port_uuid;
  identity.secondary_id = interface_uuid;
  identity.route_identity = mirror_uuid;
  identity.ifindex = *ifindex;
  identity.peer_ifindex = *peer_ifindex;
  identity.namespace_inode = endpoint.namespace_inode;
  return identity;
}

std::string address_host(std::string value) {
  const auto slash = value.find('/');
  if (slash != std::string::npos) value.resize(slash);
  return value;
}

void install_profile_flows(const GraphConfig& config, const OwnershipState& state) {
  const auto cookie = "0x" + state.owner_token.substr(0, 16);
  for (const auto& network : config.network_infrastructure.networks) {
    if (!network.profile || (*network.profile != NetworkProfile::ipvlan_l2 &&
                             *network.profile != NetworkProfile::ipvlan_l3 &&
                             *network.profile != NetworkProfile::ipvlan_l3s))
      continue;
    std::unordered_set<std::string> switches;
    for (const auto& attachment : config.network_infrastructure.attachments) {
      if (attachment.network != network.id || attachment.address.empty() ||
          (attachment.kind != AttachmentKind::container_veth &&
           attachment.kind != AttachmentKind::namespace_veth))
        continue;
      std::string ofport;
      if (run({"ovs-vsctl", "get", "Interface", attachment.peer, "ofport"}, &ofport) != 0 ||
          ofport.empty() || ofport == "-1")
        throw std::runtime_error("cannot resolve OVS port for profile attachment " + attachment.id);
      const auto address = address_host(attachment.address);
      const auto add_flow = [&](const std::string& match) {
        if (run({"ovs-ofctl", "add-flow", attachment.network_switch,
                 "cookie=" + cookie + ",priority=200," + match + ",actions=output:" + ofport}) != 0)
          throw std::runtime_error("cannot install semantic-profile flow for " + attachment.id);
      };
      add_flow("ip,nw_dst=" + address);
      add_flow("arp,arp_tpa=" + address);
      switches.insert(attachment.network_switch);
    }
    for (const auto& network_switch : switches) {
      const auto router = std::find_if(config.network_infrastructure.attachments.begin(),
                                       config.network_infrastructure.attachments.end(),
                                       [&](const auto& attachment) {
                                         return attachment.network == network.id &&
                                                attachment.network_switch == network_switch &&
                                                attachment.kind == AttachmentKind::namespace_veth;
                                       });
      if (router != config.network_infrastructure.attachments.end()) {
        std::string ofport;
        if (run({"ovs-vsctl", "get", "Interface", router->peer, "ofport"}, &ofport) != 0 ||
            ofport.empty() || ofport == "-1" ||
            run({"ovs-ofctl", "add-flow", network_switch,
                 "cookie=" + cookie +
                     ",priority=150,ip,dl_dst=00:00:00:00:00:00/01:00:00:00:00:00,actions=output:" +
                     ofport}) != 0)
          throw std::runtime_error("cannot install IPvlan router fallback flow");
      }
      if (run({"ovs-ofctl", "add-flow", network_switch,
               "cookie=" + cookie + ",priority=100,dl_dst=ff:ff:ff:ff:ff:ff,actions=drop"}) != 0 ||
          run({"ovs-ofctl", "add-flow", network_switch,
               "cookie=" + cookie +
                   ",priority=90,dl_dst=01:00:00:00:00:00/01:00:00:00:00:00,actions=drop"}) != 0)
        throw std::runtime_error("cannot install IPvlan broadcast/multicast isolation");
    }
  }
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
  if (config.version != 2) throw std::invalid_argument("OVS lifecycle requires version 2");
  const auto hash = configuration_hash(config_path);
  const auto state_path = state_root / (config.id + ".yaml");
  if (dry_run) {
    if (action == OvsLifecycleAction::create) {
      output << "# ownership-state " << state_path << "\n";
      for (const auto& item : config.network_infrastructure.switches)
        output << planned_create(item, config.id, "<generated-owner-token>", hash) << '\n';
      for (const auto& router : config.network_infrastructure.routers)
        if (router.kind == RouterKind::linux_namespace)
          output << "ip netns add " << router.namespace_name << " # owner=" << router.id << '\n';
      for (const auto& item : config.network_infrastructure.attachments) {
        if (item.kind == AttachmentKind::container_veth) {
          output << "docker[project=" << config.deployment.project << ",service=" << item.owner
                 << "] -> ip link add " << item.peer << " type veth peer name "
                 << item.interface << " -> ovs-vsctl add-port " << item.network_switch << ' '
                 << item.peer << " -> netns[verified-container] address=" << item.address
                 << " mtu=" << item.mtu << '\n';
        } else if (item.kind == AttachmentKind::namespace_veth) {
          output << "netns[" << item.owner << "] -> ip link add " << item.peer
                 << " type veth peer name " << item.interface << " -> ovs-vsctl add-port "
                 << item.network_switch << ' ' << item.peer << " address=" << item.address << '\n';
        } else if (item.kind == AttachmentKind::qemu_tap) {
          output << "ip tuntap add dev " << item.interface << " mode tap user " << item.tap_uid
                 << " group " << item.tap_gid << " -> ovs-vsctl add-port " << item.network_switch
                 << ' ' << item.interface << " mtu=" << item.mtu << '\n';
        } else if (item.kind == AttachmentKind::mirror) {
          output << "mirror " << item.id << " -> ovs-vsctl add-port " << item.network_switch << ' '
                 << item.interface << " -> OVS SPAN\n";
        } else if (item.kind == AttachmentKind::external) {
          output << "external-boundary " << item.id << " owner=" << item.owner << " unchanged\n";
        }
        for (const auto& route : item.routes) {
          output << "netns[" << item.owner << "] ip route replace " << route.destination;
          if (!route.via.empty()) output << " via " << route.via;
          output << " dev " << item.interface << '\n';
        }
      }
      for (const auto& network : config.network_infrastructure.networks) {
        if (!network.profile || (*network.profile != NetworkProfile::ipvlan_l2 &&
                                 *network.profile != NetworkProfile::ipvlan_l3 &&
                                 *network.profile != NetworkProfile::ipvlan_l3s))
          continue;
        std::unordered_set<std::string> switches;
        for (const auto& item : config.network_infrastructure.attachments) {
          if (item.network != network.id || item.address.empty() ||
              (item.kind != AttachmentKind::container_veth &&
               item.kind != AttachmentKind::namespace_veth))
            continue;
          switches.insert(item.network_switch);
          output << "ovs-ofctl add-flow " << item.network_switch
                 << " profile=" << to_string(*network.profile)
                 << " ip-dst=" << address_host(item.address)
                 << " arp-target=" << address_host(item.address)
                 << " output=<resolved-ofport:" << item.peer << ">\n";
        }
        for (const auto& network_switch : switches)
          output << "ovs-ofctl add-flow " << network_switch
                 << " ipvlan-broadcast-multicast=drop-except-targeted-arp\n";
      }
      for (const auto& router : config.network_infrastructure.routers) {
        if (router.kind != RouterKind::linux_namespace) continue;
        if (router.forwarding)
          output << "ip netns exec " << router.namespace_name
                 << " sysctl -q -w net.ipv4.ip_forward=1\n";
        for (const auto& route : router.routes) {
          output << "router-route " << router.id << ' ' << route.destination;
          if (!route.via.empty()) output << " via " << route.via;
          if (!route.device.empty()) output << " dev " << route.device;
          output << " install=" << (route.install_on_create ? "create" : "manual") << '\n';
        }
        if (!router.policies.empty())
          output << "nft add table/chain netns=" << router.namespace_name
                 << " family=inet table=graphx chain=forward policy=drop\n";
        for (const auto& policy : router.policies)
          output << "nft add rule netns=" << router.namespace_name << " policy=" << policy.id
                 << " source=" << policy.source << " destination=" << policy.destination
                 << " action=" << policy.action << '\n';
      }
    } else {
      output << "# " << (action == OvsLifecycleAction::status ? "inspect" : "identity-check-delete")
             << " ownership-state " << state_path << '\n';
    }
    return 0;
  }

  const auto lock_path = state_root / (config.id + ".lock");
  if (action == OvsLifecycleAction::status) {
    if (!inspect_existing_state_root(state_root) || !path_entry_exists(state_path)) {
      output << "No OVS ownership state for graph " << config.id << '\n';
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
    for (const auto& item : state.endpoints) {
      const auto& expected = expected_endpoint(state, item.attachment_id);
      bool owned{};
      try {
        if (expected.kind == AttachmentKind::container_veth)
          owned = container_endpoint_healthy(expected, item, state, config);
        else if (expected.kind == AttachmentKind::namespace_veth)
          owned = namespace_endpoint_healthy(expected, item, state);
        else if (expected.kind == AttachmentKind::qemu_tap)
          owned = tap_endpoint_healthy(expected, item, state);
        else
          owned = mirror_endpoint_healthy(expected, item, state);
      } catch (const std::exception& error) {
        errors << "graphx: endpoint identity check failed for " << item.attachment_id << ": "
               << error.what() << '\n';
      }
      output << to_string(expected.kind) << ' ' << item.attachment_id << " host=" << item.name;
      if (!item.container_id.empty()) output << " container=" << item.container_id;
      if (!item.tap_owner.empty()) output << " tap-owner=" << item.tap_owner;
      output << " netns-inode=" << *item.namespace_inode
             << " state=" << (owned ? "owned" : "missing-replaced-or-restarted") << '\n';
      healthy = healthy && owned;
    }
    for (const auto& item : state.namespaces) {
      const bool owned = namespace_owned(item, state);
      output << "linux_namespace " << item.name << " netns-inode=" << *item.namespace_inode
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
    OwnershipState state;
    state.graph_id = config.id;
    state.config_hash = hash;
    state.owner_token = random_token();
    state.status = "creating";
    for (const auto& attachment : config.network_infrastructure.attachments) {
      if (attachment.kind == AttachmentKind::qemu_tap) {
        state.phase = "M6";
      } else if (attachment.kind == AttachmentKind::container_veth && state.phase == "M3") {
        state.phase = "M4";
      } else if ((attachment.kind == AttachmentKind::namespace_veth ||
                  attachment.kind == AttachmentKind::mirror) &&
                 state.phase != "M6") {
        state.phase = "M5";
      }
    }
    for (const auto& network : config.network_infrastructure.networks)
      if (network.profile && *network.profile != NetworkProfile::ethernet && state.phase != "M6")
        state.phase = "M5";
    for (const auto& item : config.network_infrastructure.switches)
      state.expected_bridges.push_back(item.id);
    std::unordered_map<std::string, ResolvedContainer> containers;
    std::unordered_map<std::string, const RouterDefinition*> routers;
    for (const auto& router : config.network_infrastructure.routers) {
      if (router.kind != RouterKind::linux_namespace) continue;
      if (namespace_inode(router.namespace_name))
        throw std::runtime_error("refusing unowned Linux namespace collision: " +
                                 router.namespace_name);
      state.expected_namespaces.push_back(router.namespace_name);
      routers.emplace(router.id, &router);
    }
    struct stat host_namespace{};
    if (::stat("/proc/self/ns/net", &host_namespace) != 0)
      throw std::runtime_error("cannot inspect host network namespace");
    for (const auto& item : config.network_infrastructure.attachments) {
      if (item.kind != AttachmentKind::container_veth &&
          item.kind != AttachmentKind::namespace_veth && item.kind != AttachmentKind::qemu_tap &&
          item.kind != AttachmentKind::mirror)
        continue;
      if (item.kind != AttachmentKind::mirror && item.address.empty())
        throw std::runtime_error("M5 realization requires a declared address for attachment " +
                                 item.id);
      ExpectedEndpoint endpoint;
      endpoint.kind = item.kind;
      endpoint.id = item.id;
      endpoint.owner = item.owner;
      endpoint.host_interface = item.peer;
      endpoint.target_interface = item.interface;
      endpoint.network_switch = item.network_switch;
      endpoint.address = item.address;
      endpoint.mac = item.mac;
      endpoint.mtu = item.mtu;
      endpoint.routes = item.routes;
      endpoint.tap_uid = item.tap_uid;
      endpoint.tap_gid = item.tap_gid;
      if (item.kind == AttachmentKind::container_veth) {
        auto [container_entry, inserted] = containers.try_emplace(item.owner);
        if (inserted) container_entry->second = resolve_container(config, item.owner);
        const auto& container = container_entry->second;
        endpoint.container_id = container.id;
        endpoint.namespace_inode = container.namespace_inode;
        check_endpoint_collision(endpoint, container.pid);
      } else if (item.kind == AttachmentKind::namespace_veth) {
        const auto router = routers.find(item.owner);
        if (router == routers.end())
          throw std::runtime_error("missing Linux namespace router for " + item.id);
        endpoint.namespace_name = router->second->namespace_name;
        endpoint.namespace_inode = 1;  // Replaced with the owned inode after namespace creation.
        if (link_ifindex(endpoint.host_interface) ||
            !ovs_get("Port", endpoint.host_interface, "_uuid").empty())
          throw std::runtime_error("refusing namespace endpoint collision: " + item.id);
      } else if (item.kind == AttachmentKind::qemu_tap) {
        endpoint.host_interface = item.interface;
        endpoint.target_interface = item.interface;
        endpoint.namespace_inode = static_cast<std::uint64_t>(host_namespace.st_ino);
        if (link_ifindex(endpoint.host_interface) ||
            !ovs_get("Port", endpoint.host_interface, "_uuid").empty() ||
            !ovs_get("Interface", endpoint.host_interface, "_uuid").empty())
          throw std::runtime_error("refusing QEMU TAP collision: " + item.id);
      } else {
        const auto& network_switch =
            config.network_infrastructure.network_switch(item.network_switch);
        const auto port =
            std::find_if(network_switch.ports.begin(), network_switch.ports.end(),
                         [&](const auto& value) { return value.interface == item.interface; });
        if (port == network_switch.ports.end() || port->peer.empty())
          throw std::runtime_error("mirror attachment requires a switch port peer: " + item.id);
        endpoint.host_interface = item.interface;
        endpoint.target_interface = port->peer;
        endpoint.namespace_inode = static_cast<std::uint64_t>(host_namespace.st_ino);
        if (link_ifindex(endpoint.host_interface) || link_ifindex(endpoint.target_interface) ||
            !ovs_get("Port", endpoint.host_interface, "_uuid").empty())
          throw std::runtime_error("refusing mirror endpoint collision: " + item.id);
      }
      const auto& endpoint_switch =
          config.network_infrastructure.network_switch(endpoint.network_switch);
      const auto endpoint_port = std::find_if(endpoint_switch.ports.begin(),
                                              endpoint_switch.ports.end(), [&](const auto& value) {
                                                return value.interface == endpoint.host_interface ||
                                                       value.peer == endpoint.host_interface;
                                              });
      if (endpoint_port != endpoint_switch.ports.end()) endpoint.vlan = endpoint_port->vlan;
      state.expected_endpoints.push_back(std::move(endpoint));
    }
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
        const auto internal_port_uuid = ovs_get("Port", item.id, "_uuid");
        if (internal_port_uuid.empty())
          throw std::runtime_error("cannot capture owned OVS internal port identity");
        state.bridges.push_back(bridge_identity(item.id, uuid, internal_port_uuid));
        save_state(state_path, state);
      }
      for (const auto& [id, router] : routers) {
        output << "+ linux_namespace " << router->namespace_name << '\n';
        state.namespaces.push_back(create_namespace(*router, state));
        for (auto& endpoint : state.expected_endpoints)
          if (endpoint.owner == id && endpoint.kind == AttachmentKind::namespace_veth)
            endpoint.namespace_inode = *state.namespaces.back().namespace_inode;
        save_state(state_path, state);
        ++mutation;
        if (const auto* crash = std::getenv("GRAPHX_M5_CRASH_AFTER");
            crash && std::to_string(mutation) == crash)
          ::_exit(99);
        if (const auto* failure = std::getenv("GRAPHX_M5_FAIL_AFTER");
            failure && std::to_string(mutation) == failure)
          throw std::runtime_error("injected M5 interruption after mutation " +
                                   std::to_string(mutation));
      }
      for (const auto& endpoint : state.expected_endpoints) {
        output << "+ " << to_string(endpoint.kind) << ' ' << endpoint.id
               << " host=" << endpoint.host_interface << " switch=" << endpoint.network_switch
               << '\n';
        if (endpoint.kind == AttachmentKind::container_veth)
          state.endpoints.push_back(
              create_endpoint(endpoint, containers.at(endpoint.owner).pid, state));
        else if (endpoint.kind == AttachmentKind::namespace_veth)
          state.endpoints.push_back(create_namespace_endpoint(endpoint, state));
        else if (endpoint.kind == AttachmentKind::qemu_tap)
          state.endpoints.push_back(create_tap_endpoint(endpoint, state));
        else
          state.endpoints.push_back(create_mirror_endpoint(endpoint, state));
        ++mutation;
        if (const auto* crash = std::getenv("GRAPHX_M4_CRASH_AFTER");
            crash && std::to_string(mutation) == crash)
          ::_exit(99);
        if (const auto* failure = std::getenv("GRAPHX_M4_FAIL_AFTER");
            failure && std::to_string(mutation) == failure)
          throw std::runtime_error("injected M4 interruption after mutation " +
                                   std::to_string(mutation));
        if (const auto* crash = std::getenv("GRAPHX_M5_CRASH_AFTER");
            crash && std::to_string(mutation) == crash)
          ::_exit(99);
        if (const auto* failure = std::getenv("GRAPHX_M5_FAIL_AFTER");
            failure && std::to_string(mutation) == failure)
          throw std::runtime_error("injected M5 interruption after mutation " +
                                   std::to_string(mutation));
        if (const auto* crash = std::getenv("GRAPHX_M6_CRASH_AFTER");
            crash && std::to_string(mutation) == crash)
          ::_exit(99);
        if (const auto* failure = std::getenv("GRAPHX_M6_FAIL_AFTER");
            failure && std::to_string(mutation) == failure)
          throw std::runtime_error("injected M6 interruption after mutation " +
                                   std::to_string(mutation));
        save_state(state_path, state);
      }
      install_profile_flows(config, state);
      for (const auto& [id, router] : routers) {
        if (router->forwarding && run({"ip", "netns", "exec", router->namespace_name, "sysctl",
                                       "-q", "-w", "net.ipv4.ip_forward=1"}) != 0)
          throw std::runtime_error("cannot enable forwarding for router " + id);
        for (const auto& route : router->routes) {
          if (!route.install_on_create) continue;
          std::vector<std::string> command = {"ip", "netns", "exec",    router->namespace_name,
                                              "ip", "route", "replace", route.destination};
          if (!route.via.empty()) command.insert(command.end(), {"via", route.via});
          if (!route.device.empty()) command.insert(command.end(), {"dev", route.device});
          if (run(command) != 0) throw std::runtime_error("cannot install router route");
        }
        if (!router->policies.empty()) {
          if (run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "table", "inet",
                   "graphx"}) != 0 ||
              run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "chain", "inet",
                   "graphx", "forward", "{ type filter hook forward priority 0; policy drop; }"}) !=
                  0)
            throw std::runtime_error("cannot create router policy table");
          for (const auto& policy : router->policies)
            if (run({"ip", "netns", "exec", router->namespace_name, "nft", "add", "rule", "inet",
                     "graphx", "forward", "ip", "saddr", policy.source, "ip", "daddr",
                     policy.destination, "counter", policy.action}) != 0)
              throw std::runtime_error("cannot install router policy " + policy.id);
        }
      }
      state.status = "ready";
      save_state(state_path, state);
      output << "GraphX " << state.phase << " OVS laboratory state ready: " << state_path << '\n';
      return 0;
    } catch (...) {
      errors << "graphx: create interrupted; recovering owned OVS/container mutations\n";
      for (const auto& expected : state.expected_endpoints) {
        const bool recorded =
            std::any_of(state.endpoints.begin(), state.endpoints.end(),
                        [&](const auto& item) { return item.attachment_id == expected.id; });
        if (recorded) continue;
        const auto ifindex = link_ifindex(expected.host_interface);
        const auto port_uuid = ovs_get("Port", expected.host_interface, "_uuid");
        if (!ifindex && port_uuid.empty()) continue;
        OwnedResourceIdentity discovered;
        discovered.kind =
            expected.kind == AttachmentKind::container_veth
                ? "container_veth"
                : (expected.kind == AttachmentKind::namespace_veth
                       ? "namespace_veth"
                       : (expected.kind == AttachmentKind::qemu_tap ? "qemu_tap" : "mirror_veth"));
        discovered.attachment_id = expected.id;
        discovered.name = expected.host_interface;
        discovered.target_interface = expected.target_interface;
        discovered.ifindex = ifindex.value_or(0);
        discovered.peer_ifindex = expected.kind == AttachmentKind::mirror
                                      ? link_ifindex(expected.target_interface).value_or(0)
                                      : ifindex.value_or(0);
        discovered.namespace_inode = expected.namespace_inode;
        discovered.container_id = expected.container_id;
        if (expected.kind == AttachmentKind::qemu_tap)
          discovered.tap_owner = tap_owner_identity(expected.tap_uid, expected.tap_gid);
        discovered.stable_id = port_uuid;
        discovered.secondary_id = ovs_get("Interface", expected.host_interface, "_uuid");
        if (expected.kind == AttachmentKind::mirror)
          discovered.route_identity =
              ovs_find_uuid("Mirror", "external_ids:graphx_attachment", expected.id);
        state.endpoints.push_back(std::move(discovered));
      }
      // Include atomic mutations that happened before their ledger update.
      for (const auto& name : state.expected_bridges) {
        bool recorded{};
        for (const auto& item : state.bridges) recorded = recorded || item.name == name;
        if (recorded) continue;
        if (!bridge_exists(name)) continue;
        const auto discovered =
            bridge_identity(name, ovs_get(name, "_uuid"), ovs_get("Port", name, "_uuid"));
        if (ovs_get(name, "external_ids:graphx_owner") == state.owner_token &&
            ovs_get(name, "external_ids:graphx_config_hash") == state.config_hash &&
            ovs_get(name, "external_ids:graphx_graph") == state.graph_id)
          state.bridges.push_back(discovered);
      }
      bool cleanup_complete = true;
      for (auto iterator = state.endpoints.rbegin(); iterator != state.endpoints.rend(); ++iterator)
        if (!delete_owned_endpoint(*iterator, state)) {
          cleanup_complete = false;
          errors << "graphx: retained ownership state after rollback could not safely remove "
                 << iterator->attachment_id << '\n';
        }
      for (auto iterator = state.namespaces.rbegin(); iterator != state.namespaces.rend();
           ++iterator)
        if (!delete_owned_namespace(*iterator, state)) {
          cleanup_complete = false;
          errors << "graphx: retained ownership state after rollback could not safely remove "
                 << iterator->name << '\n';
        }
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
    throw std::runtime_error("no M5 ownership state for graph " + config.id);
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
    state.bridges.push_back(
        bridge_identity(name, ovs_get(name, "_uuid"), ovs_get("Port", name, "_uuid")));
  }
  // Recover an endpoint mutation completed before its ledger update by requiring
  // both the intrinsic host alias and OVS ownership markers.
  for (const auto& expected : state.expected_endpoints) {
    const bool recorded =
        std::any_of(state.endpoints.begin(), state.endpoints.end(),
                    [&](const auto& item) { return item.attachment_id == expected.id; });
    if (recorded) continue;
    const auto ifindex = link_ifindex(expected.host_interface);
    const auto port_uuid = ovs_get("Port", expected.host_interface, "_uuid");
    if (!ifindex && port_uuid.empty()) continue;
    if (!ifindex ||
        link_alias(expected.host_interface) != endpoint_alias(state, expected.id, "host") ||
        port_uuid.empty() ||
        ovs_get("Port", port_uuid, "external_ids:graphx_owner") != state.owner_token ||
        ovs_get("Port", port_uuid, "external_ids:graphx_attachment") != expected.id)
      throw std::runtime_error("refusing unowned or replaced container endpoint: " + expected.id);
    OwnedResourceIdentity discovered;
    discovered.kind =
        expected.kind == AttachmentKind::container_veth
            ? "container_veth"
            : (expected.kind == AttachmentKind::namespace_veth
                   ? "namespace_veth"
                   : (expected.kind == AttachmentKind::qemu_tap ? "qemu_tap" : "mirror_veth"));
    discovered.attachment_id = expected.id;
    discovered.name = expected.host_interface;
    discovered.target_interface = expected.target_interface;
    discovered.ifindex = *ifindex;
    // Container and namespace cleanup only needs the host identity. Mirrors
    // retain both host-side links, so recover their actual peer identity for
    // the complete-set replacement check.
    if (expected.kind == AttachmentKind::mirror) {
      const auto peer_ifindex = link_ifindex(expected.target_interface);
      if (!peer_ifindex ||
          link_alias(expected.target_interface) != endpoint_alias(state, expected.id, "peer"))
        throw std::runtime_error("refusing unowned or replaced mirror peer: " + expected.id);
      discovered.peer_ifindex = *peer_ifindex;
    } else {
      discovered.peer_ifindex = *ifindex;
    }
    discovered.namespace_inode = expected.namespace_inode;
    discovered.container_id = expected.container_id;
    if (expected.kind == AttachmentKind::qemu_tap) {
      if (!tap_owner_matches(expected))
        throw std::runtime_error("refusing unowned or replaced QEMU TAP: " + expected.id);
      discovered.tap_owner = tap_owner_identity(expected.tap_uid, expected.tap_gid);
    }
    discovered.stable_id = port_uuid;
    discovered.secondary_id = ovs_get("Interface", expected.host_interface, "_uuid");
    if (expected.kind == AttachmentKind::mirror)
      discovered.route_identity =
          ovs_find_uuid("Mirror", "external_ids:graphx_attachment", expected.id);
    state.endpoints.push_back(std::move(discovered));
  }
  // Refuse the entire cleanup before the first mutation when any recorded
  // resource has been replaced. Each item is checked again immediately before
  // its own deletion to close the remaining race window.
  for (const auto& item : state.bridges)
    if (bridge_exists(item.name) && !bridge_complete_set_owned(item, state))
      throw std::runtime_error("refusing OVS bridge with replaced identity or unexpected ports: " +
                               item.name);
  for (const auto& item : state.endpoints) {
    if (link_ifindex(item.name) && !host_endpoint_owned(item, state))
      throw std::runtime_error("refusing to delete replaced host endpoint: " + item.attachment_id);
    if (!ovs_get("Port", item.stable_id, "_uuid").empty() && !ovs_endpoint_owned(item, state))
      throw std::runtime_error("refusing to delete replaced OVS endpoint: " + item.attachment_id);
    if (!endpoint_names_absent_or_recorded(item))
      throw std::runtime_error("refusing same-name OVS endpoint replacement: " +
                               item.attachment_id);
    if (!item.route_identity.empty() &&
        (ovs_get("Mirror", item.route_identity, "external_ids:graphx_owner") != state.owner_token ||
         ovs_get("Mirror", item.route_identity, "external_ids:graphx_attachment") !=
             item.attachment_id))
      throw std::runtime_error("refusing replaced OVS mirror: " + item.attachment_id);
    const auto& expected = expected_endpoint(state, item.attachment_id);
    if (expected.kind == AttachmentKind::qemu_tap &&
        (!tap_owner_matches(expected) ||
         item.tap_owner != tap_owner_identity(expected.tap_uid, expected.tap_gid) ||
         !endpoint_vlan_matches(expected, item)))
      throw std::runtime_error("refusing replaced QEMU TAP ownership or VLAN: " +
                               item.attachment_id);
    if (expected.kind == AttachmentKind::mirror &&
        (link_ifindex(expected.target_interface) != item.peer_ifindex ||
         link_alias(expected.target_interface) !=
             endpoint_alias(state, item.attachment_id, "peer")))
      throw std::runtime_error("refusing replaced mirror peer: " + item.attachment_id);
  }
  for (const auto& item : state.namespaces)
    if (namespace_inode(item.name) && !namespace_owned(item, state))
      throw std::runtime_error("refusing to delete replaced Linux namespace: " + item.name);
  state.status = "destroying";
  save_state(state_path, state);
  for (auto iterator = state.endpoints.rbegin(); iterator != state.endpoints.rend(); ++iterator) {
    output << "- " << iterator->kind << ' ' << iterator->attachment_id << '\n';
    if (!delete_owned_endpoint(*iterator, state))
      throw std::runtime_error("identity changed while deleting owned container endpoint " +
                               iterator->attachment_id);
  }
  for (auto iterator = state.namespaces.rbegin(); iterator != state.namespaces.rend(); ++iterator) {
    output << "- linux_namespace " << iterator->name << '\n';
    if (!delete_owned_namespace(*iterator, state))
      throw std::runtime_error("identity changed while deleting Linux namespace " + iterator->name);
  }
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
  output << "GraphX " << state.phase << " OVS laboratory state removed\n";
  return 0;
}

}  // namespace graphx
