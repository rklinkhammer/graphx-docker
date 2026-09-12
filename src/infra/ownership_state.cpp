#include "infra/ownership_state.hpp"
#include "graphx/normalized_config.hpp"

#include <yaml-cpp/yaml.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>

namespace graphx::infra::detail {
namespace {

class Descriptor {
 public:
  explicit Descriptor(int value = -1) noexcept : value_(value) {}
  ~Descriptor() {
    if (value_ >= 0) ::close(value_);
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
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

std::string required_scalar(const YAML::Node& root, std::string_view key) {
  const auto value = root[std::string(key)];
  if (!value || !value.IsScalar() || value.Scalar().empty())
    throw std::runtime_error("invalid ownership state field: " + std::string(key));
  return value.Scalar();
}

YAML::Node state_node(const OwnershipState& state) {
  YAML::Node root;
  root["version"] = state.instance_id.empty() ? 2 : 3;
  if (!state.instance_id.empty()) {
    root["instance_id"] = state.instance_id;
    root["resource_mappings"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& mapping : state.resource_mappings) {
      YAML::Node entry;
      entry["kind"] = mapping.kind;
      entry["logical"] = mapping.logical;
      entry["physical"] = mapping.physical;
      root["resource_mappings"].push_back(entry);
    }
  }
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
  for (const auto& expected : state.expected_captures) {
    const auto& capture = expected.definition;
    YAML::Node item;
    item["id"] = capture.id;
    item["attachment"] = capture.attachment;
    item["interface"] = expected.interface;
    item["directory"] = capture.directory;
    item["snaplen"] = capture.snaplen;
    item["max_file_bytes"] = capture.max_file_bytes;
    item["max_files"] = capture.max_files;
    item["rotation_seconds"] = capture.rotation_seconds;
    item["retention_seconds"] = capture.retention_seconds;
    root["expected_captures"].push_back(item);
  }
  for (const auto& expected : state.expected_faults) {
    const auto& fault = expected.definition;
    YAML::Node item;
    item["id"] = fault.id;
    item["attachment"] = fault.attachment;
    item["interface"] = expected.interface;
    item["delay_ms"] = fault.delay_ms;
    item["jitter_ms"] = fault.jitter_ms;
    item["loss_percent"] = fault.loss_percent;
    item["rate_kbit"] = fault.rate_kbit;
    item["duration_seconds"] = fault.duration_seconds;
    root["expected_faults"].push_back(item);
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
  for (const auto& capture : state.captures) {
    YAML::Node item;
    item["kind"] = "network_capture";
    item["id"] = capture.id;
    item["attachment_id"] = capture.attachment_id;
    item["interface"] = capture.interface;
    item["session_directory"] = capture.session_directory.string();
    item["ifindex"] = capture.ifindex;
    item["directory_device"] = capture.directory_device;
    item["directory_inode"] = capture.directory_inode;
    item["directory_uid"] = capture.directory_uid;
    item["directory_gid"] = capture.directory_gid;
    item["directory_mode"] = capture.directory_mode;
    item["pid"] = capture.pid;
    item["process_start_time"] = capture.process_start_time;
    root["resources"].push_back(item);
  }
  for (const auto& fault : state.faults) {
    YAML::Node item;
    item["kind"] = "netem_fault";
    item["id"] = fault.id;
    item["attachment_id"] = fault.attachment_id;
    item["interface"] = fault.interface;
    item["ifindex"] = fault.ifindex;
    item["qdisc_identity"] = fault.qdisc_identity;
    item["timer_pid"] = fault.timer_pid;
    item["timer_start_time"] = fault.timer_start_time;
    item["boot_id"] = fault.boot_id;
    item["applied_monotonic_ns"] = fault.applied_monotonic_ns;
    item["expires_monotonic_ns"] = fault.expires_monotonic_ns;
    root["resources"].push_back(item);
  }
  return root;
}

bool hexadecimal(std::string_view value) {
  return std::ranges::all_of(value,
                             [](unsigned char character) { return std::isxdigit(character) != 0; });
}

}  // namespace

OwnedResourceIdentity bridge_identity(std::string name, std::string uuid,
                                      std::string internal_port_uuid) {
  OwnedResourceIdentity identity;
  identity.kind = "ovs_bridge";
  identity.name = std::move(name);
  identity.stable_id = std::move(uuid);
  identity.secondary_id = std::move(internal_port_uuid);
  return identity;
}

bool stable_identity_matches(const OwnedResourceIdentity& expected,
                             const OwnedResourceIdentity& observed) {
  return expected.kind == observed.kind && expected.name == observed.name &&
         expected.stable_id == observed.stable_id &&
         expected.secondary_id == observed.secondary_id &&
         expected.attachment_id == observed.attachment_id &&
         expected.target_interface == observed.target_interface &&
         expected.ifindex == observed.ifindex && expected.peer_ifindex == observed.peer_ifindex &&
         expected.namespace_inode == observed.namespace_inode &&
         expected.container_id == observed.container_id &&
         expected.tap_owner == observed.tap_owner &&
         expected.route_identity == observed.route_identity &&
         expected.rule_identity == observed.rule_identity &&
         expected.qdisc_identity == observed.qdisc_identity &&
         expected.capture_identity == observed.capture_identity &&
         expected.process_identity == observed.process_identity;
}

std::string configuration_hash(const std::filesystem::path& path, const GraphConfig* config) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open configuration for hashing: " + path.string());
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (!context) throw std::runtime_error("cannot allocate configuration hash context");
  const auto cleanup = [&] { EVP_MD_CTX_free(context); };
  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
    cleanup();
    throw std::runtime_error("cannot initialize configuration hash");
  }
  std::array<char, 8192> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (input.gcount() > 0 &&
        EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(input.gcount())) != 1) {
      cleanup();
      throw std::runtime_error("cannot update configuration hash");
    }
  }
  if (!input.eof()) {
    cleanup();
    throw std::runtime_error("cannot read configuration for hashing");
  }
  if (config != nullptr && !config->deployment.instance_id.empty()) {
    const auto normalized = normalize_config_json(*config);
    if (EVP_DigestUpdate(context, normalized.data(), normalized.size()) != 1) {
      cleanup();
      throw std::runtime_error("cannot hash effective instance configuration");
    }
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size{};
  if (EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1) {
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

bool path_entry_exists(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (!error) return status.type() != std::filesystem::file_type::not_found;
  if (error == std::errc::no_such_file_or_directory) return false;
  throw std::runtime_error("cannot inspect ownership path " + path.string() + ": " +
                           error.message());
}

void save_state(const std::filesystem::path& path, const OwnershipState& state,
                bool replace_existing) {
  YAML::Emitter emitter;
  emitter.SetIndent(2);
  emitter << state_node(state);
  if (!emitter.good()) throw std::runtime_error("cannot serialize ownership state");
  const auto temporary = path.string() + ".tmp." + std::to_string(::getpid());
  const Descriptor descriptor(
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
    if (::link(temporary.c_str(), path.c_str()) != 0) {
      const auto failure = errno;
      ::unlink(temporary.c_str());
      throw std::system_error(failure, std::generic_category(),
                              "cannot exclusively publish ownership state");
    }
    ::unlink(temporary.c_str());
  }
  const Descriptor directory(
      ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  if (directory.get() < 0 || ::fsync(directory.get()) != 0)
    throw std::system_error(errno, std::generic_category(),
                            "cannot sync ownership state directory");
}

OwnershipState load_state(const std::filesystem::path& path) {
  const Descriptor descriptor(::open(path.c_str(), O_RDONLY | O_NOFOLLOW));
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
  if (!root.IsMap()) throw std::runtime_error("unsupported ownership state format");
  const auto version = root["version"].as<int>(0);
  if (version != 2 && version != 3) throw std::runtime_error("unsupported ownership state format");
  OwnershipState state;
  state.graph_id = required_scalar(root, "graph_id");
  if (version == 3) {
    state.instance_id = required_scalar(root, "instance_id");
    const auto mappings = root["resource_mappings"];
    if (!mappings || !mappings.IsSequence() || mappings.size() == 0)
      throw std::runtime_error("invalid instance resource mappings");
    std::unordered_set<std::string> physical;
    for (const auto& entry : mappings) {
      InstanceResourceMapping mapping{required_scalar(entry, "kind"),
                                      required_scalar(entry, "logical"),
                                      required_scalar(entry, "physical")};
      if (mapping.physical != instance_resource_name(state.graph_id, state.instance_id,
                                                     mapping.kind, mapping.logical) ||
          !physical.insert(mapping.physical).second)
        throw std::runtime_error("invalid or duplicate instance resource mapping");
      state.resource_mappings.push_back(std::move(mapping));
    }
    if (!physical.contains(
            instance_resource_name(state.graph_id, state.instance_id, "state", state.graph_id)))
      throw std::runtime_error("missing instance state mapping");
  } else if (root["instance_id"] || root["resource_mappings"]) {
    throw std::runtime_error("legacy ledger cannot contain instance identity");
  }
  state.config_hash = required_scalar(root, "config_sha256");
  state.owner_token = required_scalar(root, "owner_token");
  state.status = required_scalar(root, "status");
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
  std::unordered_set<std::string> expected_capture_ids;
  if (root["expected_captures"])
    for (const auto& value : root["expected_captures"]) {
      ExpectedCapture expected_capture;
      auto& capture = expected_capture.definition;
      capture.id = required_scalar(value, "id");
      capture.attachment = required_scalar(value, "attachment");
      expected_capture.interface = required_scalar(value, "interface");
      capture.directory = required_scalar(value, "directory");
      capture.snaplen = value["snaplen"].as<std::uint32_t>(0);
      capture.max_file_bytes = value["max_file_bytes"].as<std::uint64_t>(0);
      capture.max_files = value["max_files"].as<std::uint32_t>(0);
      capture.rotation_seconds = value["rotation_seconds"].as<std::uint32_t>(0);
      capture.retention_seconds = value["retention_seconds"].as<std::uint32_t>(0);
      if (!expected_capture_ids.insert(capture.id).second || capture.snaplen < 256 ||
          capture.max_file_bytes < 65536 || capture.max_files == 0 ||
          capture.rotation_seconds == 0 || capture.retention_seconds < capture.rotation_seconds)
        throw std::runtime_error("invalid expected network capture");
      state.expected_captures.push_back(std::move(expected_capture));
    }
  std::unordered_set<std::string> expected_fault_ids;
  if (root["expected_faults"])
    for (const auto& value : root["expected_faults"]) {
      ExpectedFault expected_fault;
      auto& fault = expected_fault.definition;
      fault.id = required_scalar(value, "id");
      fault.attachment = required_scalar(value, "attachment");
      expected_fault.interface = required_scalar(value, "interface");
      fault.delay_ms = value["delay_ms"].as<std::uint32_t>(0);
      fault.jitter_ms = value["jitter_ms"].as<std::uint32_t>(0);
      fault.loss_percent = value["loss_percent"].as<double>(0.0);
      fault.rate_kbit = value["rate_kbit"].as<std::uint32_t>(0);
      fault.duration_seconds = value["duration_seconds"].as<std::uint32_t>(0);
      if (!expected_fault_ids.insert(fault.id).second || fault.duration_seconds == 0 ||
          (fault.delay_ms == 0 && fault.loss_percent == 0.0 && fault.rate_kbit == 0))
        throw std::runtime_error("invalid expected network fault");
      state.expected_faults.push_back(std::move(expected_fault));
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
      if (kind == "network_capture") {
        OwnedCapture capture;
        capture.id = required_scalar(value, "id");
        capture.attachment_id = required_scalar(value, "attachment_id");
        capture.interface = required_scalar(value, "interface");
        capture.session_directory = required_scalar(value, "session_directory");
        capture.ifindex = value["ifindex"].as<std::uint32_t>(0);
        capture.directory_device = value["directory_device"].as<std::uint64_t>(0);
        capture.directory_inode = value["directory_inode"].as<std::uint64_t>(0);
        capture.directory_uid = value["directory_uid"].as<std::uint32_t>(UINT32_MAX);
        capture.directory_gid = value["directory_gid"].as<std::uint32_t>(UINT32_MAX);
        capture.directory_mode = value["directory_mode"].as<std::uint32_t>(0);
        capture.pid = value["pid"].as<std::uint32_t>(0);
        capture.process_start_time = required_scalar(value, "process_start_time");
        if (!expected_capture_ids.contains(capture.id) || !capture.ifindex ||
            !capture.directory_device || !capture.directory_inode || capture.directory_uid != 0 ||
            capture.directory_gid != 0 || capture.directory_mode != 0700 || !capture.pid)
          throw std::runtime_error("invalid owned network capture");
        state.captures.push_back(std::move(capture));
        continue;
      }
      if (kind == "netem_fault") {
        OwnedFault fault;
        fault.id = required_scalar(value, "id");
        fault.attachment_id = required_scalar(value, "attachment_id");
        fault.interface = required_scalar(value, "interface");
        fault.ifindex = value["ifindex"].as<std::uint32_t>(0);
        fault.qdisc_identity = required_scalar(value, "qdisc_identity");
        fault.timer_pid = value["timer_pid"].as<std::uint32_t>(0);
        fault.timer_start_time = required_scalar(value, "timer_start_time");
        fault.boot_id = required_scalar(value, "boot_id");
        fault.applied_monotonic_ns = value["applied_monotonic_ns"].as<std::uint64_t>(0);
        fault.expires_monotonic_ns = value["expires_monotonic_ns"].as<std::uint64_t>(0);
        const auto expected_fault = std::find_if(
            state.expected_faults.begin(), state.expected_faults.end(),
            [&](const auto& candidate) { return candidate.definition.id == fault.id; });
        const auto expected_duration =
            expected_fault == state.expected_faults.end()
                ? 0
                : static_cast<std::uint64_t>(expected_fault->definition.duration_seconds) *
                      1'000'000'000ULL;
        if (!expected_fault_ids.contains(fault.id) || !fault.ifindex || !fault.timer_pid ||
            fault.qdisc_identity.find("netem") == std::string::npos || fault.boot_id.size() != 36 ||
            !fault.applied_monotonic_ns ||
            fault.expires_monotonic_ns <= fault.applied_monotonic_ns ||
            fault.expires_monotonic_ns - fault.applied_monotonic_ns != expected_duration)
          throw std::runtime_error("invalid owned netem fault");
        state.faults.push_back(std::move(fault));
        continue;
      }
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

}  // namespace graphx::infra::detail
