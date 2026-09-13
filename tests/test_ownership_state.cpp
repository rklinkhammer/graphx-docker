#include "infra/ownership_lock.hpp"
#include "infra/ownership_state.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using graphx::OwnedResourceIdentity;
using graphx::infra::detail::configuration_hash;
using graphx::infra::detail::ensure_state_root;
using graphx::infra::detail::load_state;
using graphx::infra::detail::OwnershipLock;
using graphx::infra::detail::OwnershipLockMode;
using graphx::infra::detail::OwnershipState;
using graphx::infra::detail::save_state;
using graphx::infra::detail::stable_identity_matches;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    std::array<char, 64> pattern{};
    const auto value = std::string("/tmp/graphx-ownership-state-XXXXXX");
    std::copy(value.begin(), value.end(), pattern.begin());
    const auto* created = ::mkdtemp(pattern.data());
    if (created == nullptr) throw std::runtime_error("cannot create temporary directory");
    path_ = created;
    if (::chmod(path_.c_str(), 0700) != 0)
      throw std::runtime_error("cannot secure temporary directory");
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

void expect_failure(const std::function<void()>& operation, std::string_view message) {
  try {
    operation();
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

void write_file(const std::filesystem::path& path, std::string_view contents, mode_t mode = 0600) {
  std::ofstream output(path, std::ios::binary);
  output << contents;
  output.close();
  if (!output || ::chmod(path.c_str(), mode) != 0)
    throw std::runtime_error("cannot create test file");
}

std::filesystem::path secure_fixture_copy(const TemporaryDirectory& temporary,
                                          std::string_view fixture) {
  const auto source = std::filesystem::path(GRAPHX_SOURCE_DIR) / "tests" / "fixtures" /
                      "ownership" / (std::string(fixture) + ".yaml");
  const auto destination = temporary.path() / (std::string(fixture) + ".yaml");
  std::filesystem::copy_file(source, destination);
  if (::chmod(destination.c_str(), 0600) != 0)
    throw std::runtime_error("cannot secure fixture copy");
  return destination;
}

void test_round_trip_and_publication() {
  TemporaryDirectory temporary;
  const auto source = secure_fixture_copy(temporary, "current");
  const auto original = load_state(source);
  const auto published = temporary.path() / "published.yaml";
  save_state(published, original, false);
  std::ifstream serialized(published);
  const std::string contents((std::istreambuf_iterator<char>(serialized)),
                             std::istreambuf_iterator<char>());
  require(contents.find("version: 2") != std::string::npos &&
              contents.find("phase:") == std::string::npos,
          "current ledger format");
  const auto loaded = load_state(published);
  require(loaded.graph_id == original.graph_id && loaded.config_hash == original.config_hash &&
              loaded.owner_token == original.owner_token && loaded.status == original.status &&
              loaded.expected_bridges == original.expected_bridges,
          "ledger round trip");

  expect_failure([&] { save_state(published, OwnershipState{}, false); },
                 "exclusive publication replaced a ledger");
  require(load_state(published).graph_id == original.graph_id,
          "failed publication changed existing ledger");

  const auto directory_target = temporary.path() / "directory-target.yaml";
  std::filesystem::create_directory(directory_target);
  expect_failure([&] { save_state(directory_target, original, true); },
                 "publication over a directory succeeded");
  require(std::filesystem::is_directory(directory_target),
          "failed atomic publication changed destination");
  require(
      !std::filesystem::exists(directory_target.string() + ".tmp." + std::to_string(::getpid())),
      "failed atomic publication left temporary state");
}

void test_network_extensions() {
  TemporaryDirectory temporary;
  auto state = load_state(secure_fixture_copy(temporary, "current"));
  graphx::infra::detail::ExpectedEndpoint endpoint;
  endpoint.id = "right-data";
  endpoint.owner = "right";
  endpoint.kind = graphx::AttachmentKind::namespace_veth;
  endpoint.host_interface = "gxpright";
  endpoint.target_interface = "gxiright";
  endpoint.network_switch = "gxbright";
  endpoint.namespace_name = "gxright";
  endpoint.namespace_inode = 1;
  endpoint.address = "10.64.3.10/24";
  endpoint.aliases = {"10.64.30.10/32"};
  endpoint.management_policy = std::string(64, 'a');
  endpoint.routes.push_back({"10.65.0.0/24", "10.64.3.1", {}, false});
  state.expected_endpoints.push_back(endpoint);
  state.handoff_name = "handoff-" + std::string(32, 'a');
  state.handoff_inode = 42;
  const auto path = temporary.path() / "network.yaml";
  save_state(path, state);
  auto loaded = load_state(path);
  require(loaded.expected_endpoints.front().aliases == endpoint.aliases &&
              loaded.expected_endpoints.front().management_policy == endpoint.management_policy &&
              !loaded.expected_endpoints.front().routes.front().install_on_create &&
              loaded.handoff_inode == 42,
          "network extension round trip");
  loaded.expected_endpoints.front().aliases.push_back(endpoint.aliases.front());
  save_state(path, loaded);
  expect_failure([&] { static_cast<void>(load_state(path)); }, "duplicate aliases accepted");
  state.handoff_name = "../foreign";
  save_state(path, state);
  expect_failure([&] { static_cast<void>(load_state(path)); }, "handoff traversal accepted");
  state.handoff_name = "handoff-" + std::string(32, 'a');
  state.expected_endpoints.front().management_policy = "forged";
  save_state(path, state);
  expect_failure([&] { static_cast<void>(load_state(path)); }, "invalid policy identity accepted");
}
void test_process_inventory() {
  TemporaryDirectory temporary;
  auto state = load_state(secure_fixture_copy(temporary, "current"));
  state.expected_bridges.clear();
  state.bridges.clear();
  OwnedResourceIdentity resource;
  resource.kind = "container";
  resource.name = "graphx-" + state.graph_id + "-platform";
  resource.stable_id = "pending";
  resource.secondary_id = "sha256:" + std::string(64, 'a');
  resource.process_identity = state.owner_token;
  state.processes.push_back(resource);
  const auto path = temporary.path() / "processes.yaml";
  save_state(path, state);
  const auto loaded = load_state(path);
  require(loaded.expected_bridges.empty() && loaded.processes.size() == 1 &&
              stable_identity_matches(loaded.processes.front(), resource),
          "process inventory round trip");
  for (const auto& invalid :
       {std::string("owner"), std::string("path"), std::string("kind"), std::string("duplicate")}) {
    auto changed = state;
    if (invalid == "owner") changed.processes.front().process_identity = std::string(32, '0');
    if (invalid == "path") changed.processes.front().name += "/../../foreign";
    if (invalid == "kind") changed.processes.front().kind = "unknown";
    if (invalid == "duplicate") changed.processes.push_back(resource);
    save_state(path, changed);
    expect_failure([&] { static_cast<void>(load_state(path)); },
                   "invalid process scope was accepted");
  }
}

void test_rejected_state_files() {
  TemporaryDirectory temporary;
  const auto malformed = temporary.path() / "malformed.yaml";
  write_file(malformed, "version: [\n");
  expect_failure([&] { static_cast<void>(load_state(malformed)); }, "malformed state was accepted");

  const auto truncated = temporary.path() / "truncated.yaml";
  write_file(truncated, "version: 2\n");
  expect_failure([&] { static_cast<void>(load_state(truncated)); }, "truncated state was accepted");

  const auto insecure = secure_fixture_copy(temporary, "current");
  require(::chmod(insecure.c_str(), 0644) == 0, "cannot make fixture insecure");
  expect_failure([&] { static_cast<void>(load_state(insecure)); }, "insecure state was accepted");
  std::filesystem::remove(insecure);

  const auto target = secure_fixture_copy(temporary, "current");
  const auto symlink = temporary.path() / "state-link.yaml";
  std::filesystem::create_symlink(target, symlink);
  expect_failure([&] { static_cast<void>(load_state(symlink)); }, "symlink state was accepted");
}

void test_state_root_and_lock_security() {
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "state";
  ensure_state_root(root);
  struct stat metadata{};
  require(::lstat(root.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0700,
          "state root mode");

  const auto insecure_root = temporary.path() / "insecure-state";
  std::filesystem::create_directory(insecure_root);
  require(::chmod(insecure_root.c_str(), 0755) == 0, "cannot make state root insecure");
  expect_failure([&] { ensure_state_root(insecure_root); }, "insecure state root was accepted");
  const auto symlink_root = temporary.path() / "state-link";
  std::filesystem::create_directory(temporary.path() / "state-target");
  std::filesystem::create_symlink(temporary.path() / "state-target", symlink_root);
  expect_failure([&] { ensure_state_root(symlink_root); }, "symlink state root was accepted");

  const auto lock_path = root / "graph.lock";
  {
    auto lock = OwnershipLock::open_or_create(lock_path, OwnershipLockMode::exclusive);
    require(::lstat(lock_path.c_str(), &metadata) == 0 && S_ISREG(metadata.st_mode) &&
                metadata.st_size == 0 && (metadata.st_mode & 0777) == 0600,
            "persistent lock metadata");
    expect_failure(
        [&] {
          auto contender = OwnershipLock::open_existing(lock_path, OwnershipLockMode::shared);
          static_cast<void>(contender);
        },
        "contended lock was acquired");
  }
  require(std::filesystem::exists(lock_path) && std::filesystem::file_size(lock_path) == 0,
          "lock did not persist as zero-byte file");
  auto shared = OwnershipLock::open_existing(lock_path, OwnershipLockMode::shared);
  static_cast<void>(shared);

  const auto nonempty_lock = root / "nonempty.lock";
  write_file(nonempty_lock, "not-a-lock");
  expect_failure(
      [&] {
        auto lock = OwnershipLock::open_existing(nonempty_lock, OwnershipLockMode::shared);
        static_cast<void>(lock);
      },
      "nonempty lock was accepted");

  const auto symlink_lock = root / "link.lock";
  std::filesystem::create_symlink(lock_path, symlink_lock);
  expect_failure(
      [&] {
        auto lock = OwnershipLock::open_existing(symlink_lock, OwnershipLockMode::shared);
        static_cast<void>(lock);
      },
      "symlink lock was accepted");
}

void test_identity_and_hash_helpers() {
  OwnedResourceIdentity expected;
  expected.kind = "ovs_bridge";
  expected.name = "br-collision";
  expected.stable_id = "11111111-1111-1111-1111-111111111111";
  auto observed = expected;
  require(stable_identity_matches(expected, observed), "equal stable identity rejected");
  observed.stable_id = "22222222-2222-2222-2222-222222222222";
  require(!stable_identity_matches(expected, observed),
          "same-name replacement authorized by identity comparison");

  TemporaryDirectory temporary;
  const auto input = temporary.path() / "hash-input";
  write_file(input, "abc");
  require(configuration_hash(input) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "configuration SHA-256");
}

}  // namespace

int main() {
  try {
    test_round_trip_and_publication();
    test_process_inventory();
    test_network_extensions();
    test_rejected_state_files();
    test_state_root_and_lock_security();
    test_identity_and_hash_helpers();
    std::cout << "GraphX ownership state tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ownership state test failed: " << error.what() << '\n';
    return 1;
  }
}
