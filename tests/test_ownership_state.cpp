#include "infra/ownership_lock.hpp"
#include "infra/application_lifecycle.hpp"
#include "infra/process_resources.hpp"
#include <thread>
#include "infra/endpoint_resources.hpp"
#include "infra/ownership_state.hpp"

#include <sys/stat.h>
#include <sys/wait.h>
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

void test_scenario_recovery_records() {
  TemporaryDirectory temporary;
  const auto path = secure_fixture_copy(temporary, "current");
  auto state = load_state(path);
  state.actions.push_back({"route", "route-apply", "pending", ""});
  save_state(path, state);
  require(load_state(path).actions.front().status == "pending", "interrupted scenario intent lost");
  state.actions.front().kind = "shell";
  save_state(path, state);
  expect_failure([&] { (void)load_state(path); }, "unknown scenario operation accepted");
  state.actions.front().kind = "route-apply";
  state.actions.push_back(state.actions.front());
  save_state(path, state);
  expect_failure([&] { (void)load_state(path); }, "duplicate scenario identity accepted");
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
void test_passive_mirror_contract() {
  using graphx::infra::detail::jumbo_offloads_disabled;
  using graphx::infra::detail::mirror_peer_link_matches;
  using graphx::infra::detail::passive_mirror_filter_matches;
  const std::string offloads =
      "tcp-segmentation-offload: off\ngeneric-segmentation-offload: off\ngeneric-receive-offload: "
      "off\ntx-checksumming: off\n";
  require(jumbo_offloads_disabled(offloads), "disabled offloads rejected");
  require(!jumbo_offloads_disabled(""), "missing offload evidence accepted");
  require(!jumbo_offloads_disabled(offloads + "generic-receive-offload: on\n"),
          "duplicate/enabled GRO accepted");
  for (std::size_t pos = 0; (pos = offloads.find("off\n", pos)) != std::string::npos; pos += 4) {
    auto enabled = offloads;
    enabled.replace(pos, 3, "on");
    require(!jumbo_offloads_disabled(enabled), "active jumbo offload accepted");
  }
  require(mirror_peer_link_matches("43: gxpeer: mtu 9000 alias owned", 43, "owned"),
          "peer identity rejected");
  require(!mirror_peer_link_matches("44: gxpeer: mtu 9000 alias owned", 43, "owned"),
          "replacement index accepted");
  require(!mirror_peer_link_matches("43: gxpeer: mtu 9000 alias owned-foreign", 43, "owned"),
          "alias prefix accepted");
  require(!mirror_peer_link_matches("43: gxpeer: mtu 9000 alias foreign-owned", 43, "owned"),
          "alias substring accepted");
  const std::string filter =
      R"([{"kind":"matchall","protocol":"all","pref":1,"chain":0,"options":{"actions":[{"kind":"gact","control_action":{"type":"drop"}}]}}])";
  require(passive_mirror_filter_matches(filter), "valid passive filter rejected");
  const std::string header = R"({"protocol":"all","pref":1,"kind":"matchall","chain":0})";
  const auto reported = "[" + header + "," + filter.substr(1);
  require(passive_mirror_filter_matches(reported), "iproute2 classifier header rejected");
  require(!passive_mirror_filter_matches("[" + header + "]"), "header without rule accepted");
  require(!passive_mirror_filter_matches("[" + header + "," + header + "]"),
          "two headers accepted without a rule");
  require(
      !passive_mirror_filter_matches(filter.substr(0, filter.size() - 1) + "," + filter.substr(1)),
      "two actual filter rules accepted");
  for (const auto& token : {std::string("\"pref\":1"), std::string("\"chain\":0")}) {
    auto changed = reported;
    changed.replace(changed.find(token), token.size(), token.substr(0, token.size() - 1) + "2");
    require(!passive_mirror_filter_matches(changed), "unrelated classifier header accepted");
  }
  for (const auto& invalid :
       {std::string("[]"), std::string("{"), std::string("{}"), filter + filter})
    require(!passive_mirror_filter_matches(invalid), "malformed filter accepted");
  for (const auto& [from, to] :
       std::array<std::pair<std::string, std::string>, 4>{{{"drop", "pass"},
                                                           {"matchall", "flower"},
                                                           {"all", "ip"},
                                                           {"\"pref\":1", "\"pref\":2"}}}) {
    auto changed = filter;
    changed.replace(changed.find(from), from.size(), to);
    require(!passive_mirror_filter_matches(changed), "non-passive filter accepted");
  }
  TemporaryDirectory temporary;
  auto state = load_state(secure_fixture_copy(temporary, "current"));
  graphx::infra::detail::ExpectedEndpoint endpoint;
  endpoint.id = "mirror";
  endpoint.owner = "recorder";
  endpoint.kind = graphx::AttachmentKind::mirror;
  endpoint.host_interface = "gxmirror";
  endpoint.target_interface = "gxpeer";
  endpoint.network_switch = "gxbridge";
  endpoint.namespace_inode = 7;
  endpoint.container_id = std::string(64, 'a');
  endpoint.mirror_container = true;
  endpoint.mtu = 9000;
  state.expected_endpoints.push_back(endpoint);
  const auto path = temporary.path() / "mirror.yaml";
  save_state(path, state);
  const auto loaded = load_state(path).expected_endpoints.back();
  require(loaded.mirror_container && loaded.mtu == 9000 &&
              loaded.container_id == endpoint.container_id && loaded.namespace_inode == 7,
          "mirror ownership roundtrip");
  OwnedResourceIdentity mirror;
  mirror.kind = "mirror_veth";
  mirror.attachment_id = endpoint.id;
  mirror.name = endpoint.host_interface;
  mirror.target_interface = endpoint.target_interface;
  mirror.container_id = endpoint.container_id;
  mirror.namespace_inode = endpoint.namespace_inode;
  mirror.ifindex = 42;
  mirror.peer_ifindex = 43;
  mirror.stable_id = "11111111-1111-1111-1111-111111111111";
  mirror.secondary_id = "22222222-2222-2222-2222-222222222222";
  mirror.route_identity = "33333333-3333-3333-3333-333333333333";
  state.status = "creating";
  state.endpoints.push_back(mirror);
  save_state(path, state);
  require(stable_identity_matches(mirror, load_state(path).endpoints.back()),
          "interrupted mirror identity lost");
  for (int field = 0; field < 3; ++field) {
    auto replaced = state;
    if (field == 0) replaced.endpoints.back().namespace_inode = 8;
    if (field == 1) replaced.endpoints.back().container_id = std::string(64, 'b');
    if (field == 2) replaced.endpoints.back().target_interface = "foreign";
    save_state(path, replaced);
    expect_failure([&] { static_cast<void>(load_state(path)); },
                   "replaced mirror identity accepted for cleanup");
  }
  // Ordinary container veths must enforce the same redundant ledger identity
  // as passive mirrors, before cleanup can stop processes or delete resources.
  auto data_state = state;
  data_state.expected_endpoints.back().mirror_container = false;
  data_state.expected_endpoints.back().kind = graphx::AttachmentKind::container_veth;
  data_state.endpoints.back().kind = "container_veth";
  data_state.endpoints.back().route_identity.clear();
  save_state(path, data_state);
  static_cast<void>(load_state(path));
  for (int field = 0; field < 5; ++field) {
    auto corrupt = data_state;
    auto& item = corrupt.endpoints.back();
    if (field == 0) item.namespace_inode = 8;
    if (field == 1) item.container_id = std::string(64, 'b');
    if (field == 2) item.target_interface = "foreign";
    if (field == 3) item.name = "foreign";
    if (field == 4) item.kind = "mirror_veth";
    save_state(path, corrupt);
    expect_failure([&] { static_cast<void>(load_state(path)); },
                   "replaced container veth identity accepted for cleanup");
  }
  auto replaced = mirror;
  replaced.peer_ifindex = 44;
  require(!stable_identity_matches(mirror, replaced), "replacement peer authorized");
  for (int failure = 0; failure < 4; ++failure) {
    auto changed = state;
    auto& item = changed.expected_endpoints.back();
    if (failure == 0) item.mtu = 9001;
    if (failure == 1) item.mtu = 0;
    if (failure == 2) item.container_id.clear();
    if (failure == 3) item.kind = graphx::AttachmentKind::namespace_veth;
    save_state(path, changed);
    expect_failure([&] { static_cast<void>(load_state(path)); }, "invalid mirror ledger accepted");
  }
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

void test_growing_readiness_log() {
  using namespace graphx::infra::detail;
  TemporaryDirectory temporary;
  const auto root = std::filesystem::canonical(temporary.path());
  const auto log = root / "application.log";
  write_file(log, "ready node=radio\n");
  {
    std::jthread writer([&] {
      std::ofstream stream(log, std::ios::app);
      for (int i = 0; i < 1000; ++i) stream << std::string(64, 'x') << std::flush;
    });
    for (int i = 0; i < 1000; ++i) {
      const auto snapshot = read_process_log(log, 131072);
      require(snapshot.starts_with("ready node=radio\n") && snapshot.size() <= 131072,
              "growing log lost readiness or exceeded storage");
    }
  }
  expect_failure([&] { (void)read_process_log(log, 8); }, "oversized log accepted");
  const auto link = root / "log-link";
  std::filesystem::create_symlink(log, link);
  expect_failure([&] { (void)read_process_log(link, 131072); }, "symlink log accepted");
  std::filesystem::create_hard_link(log, root / "log-hardlink");
  expect_failure([&] { (void)read_process_log(log, 131072); }, "hardlinked log accepted");
}

void test_application_admission() {
  using namespace graphx::infra::detail;
  using graphx::config_internal::parse_document;
  TemporaryDirectory temporary;
  const auto path = secure_fixture_copy(temporary, "current");
  auto state = load_state(path);
  state.applications = {{"radio", "ready"}, {"recorder", "timeout"}};
  save_state(path, state);
  require(load_state(path).applications == state.applications, "application admission lost");
  state.applications["recorder"] = "restarting";
  save_state(path, state);
  expect_failure([&] { (void)load_state(path); }, "unknown admission accepted");
  auto graph = parse_document(R"({"nodes":[{"node_id":"radio","type":"vita.radio"},
    {"node_id":"processor","type":"vita.processor"},{"node_id":"recorder","type":"vita.recorder"}]})");
  require(application_status(graph, {{"radio", true}, {"processor", true}, {"recorder", true}}) ==
              "ready",
          "complete graph");
  require(application_status(graph, {{"radio", true}, {"processor", true}}) == "degraded",
          "recorder dependency");
  require(application_status(graph, {{"radio", true}, {"recorder", true}}) == "unavailable",
          "missing controller fabricated acquisition");
  require(application_status(graph, {{"processor", true}, {"recorder", true}}) == "unavailable",
          "missing radios fabricated acquisition");
  require(application_ready("ready node=radio", "radio") &&
              application_ready("hello\nready node=radio\n", "radio"),
          "trimmed readiness lost");
  require(!application_ready("not ready node=radio", "radio") &&
              !application_ready("ready node=radio2", "radio"),
          "ambiguous readiness accepted");
  graph["nodes"].array().push_back(
      parse_document(R"({"node_id":"processor2","type":"vita.processor"})"));
  require(application_status(
              graph, {{"radio", true}, {"processor", true}, {"processor2", false}}) == "degraded",
          "one failed processor hid surviving acquisition");
  require(!available_startup(graph), "transactional default changed");
  require(!unavailable_exit(1) && !unavailable_exit(78), "unknown/security errors ignored");
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

void test_owned_container_services() {
  using graphx::infra::detail::bind_owned_container_services;
  graphx::GraphConfig config;
  config.id = "mirror-scenario";
  OwnershipState state;
  state.graph_id = config.id;
  OwnedResourceIdentity recorder;
  recorder.kind = "container";
  recorder.name = "graphx-mirror-scenario-recorder";
  recorder.secondary_id = "sha256:recorder-image";
  state.processes.push_back(recorder);
  bind_owned_container_services(config, state);
  require(config.deployment.project == "graphx-mirror-scenario" &&
              config.deployment.services.size() == 1 &&
              config.deployment.services.front().node_id == "recorder" &&
              config.deployment.services.front().image == recorder.secondary_id,
          "mirror service missing from owned deployment");
  auto bad = state;
  bad.graph_id = "foreign";
  expect_failure([&] { bind_owned_container_services(config, bad); }, "foreign ledger accepted");
  bad = state;
  bad.processes.front().name = "graphx-foreign-recorder";
  expect_failure([&] { bind_owned_container_services(config, bad); }, "foreign service accepted");
  bad = state;
  bad.processes.push_back(recorder);
  expect_failure([&] { bind_owned_container_services(config, bad); }, "duplicate service accepted");
}

void test_retained_only() {
  using graphx::infra::detail::retained_only;
  OwnershipState state;
  state.graph_id = "recovery";
  state.status = "destroying";
  OwnedResourceIdentity history;
  history.kind = "volume";
  history.name = "graphx-recovery-history";
  history.stable_id = history.name;
  state.processes.push_back(history);
  require(retained_only(state), "retained history prevents explicit recovery");
  auto bad = state;
  bad.status = "ready";
  require(!retained_only(bad), "active state classified inactive");
  bad = state;
  bad.expected_bridges.push_back("bridge");
  require(!retained_only(bad), "unfinished network cleanup classified inactive");
  bad = state;
  bad.processes.front().kind = "container";
  require(!retained_only(bad), "remaining container classified inactive");
  bad = state;
  bad.processes.front().name = "graphx-recovery-credentials";
  require(!retained_only(bad), "remaining credentials classified inactive");
  bad = state;
  bad.processes.front().stable_id = "pending";
  require(!retained_only(bad), "pending volume classified inactive");
}

void test_owned_process_exit(const std::filesystem::path& executable) {
  using namespace graphx::infra::detail;
  TemporaryDirectory directory;
  const auto root = std::filesystem::canonical(directory.path());
  NativeProcessOptions options;
  options.id = "early-exit";
  options.executable = executable;
  options.cwd = root;
  options.argv = {executable.string(), "--exit-fixture"};
  // Exercise exit while the parent observes identity; admission must retain the
  // real failure classification instead of authorizing an unknown live process.
  for (int attempt = 0; attempt < 64; ++attempt) {
    options.log = root / ("exit-" + std::to_string(attempt) + ".log");
    bool registered = false;
    const auto process = start_native_process(options, [&](const auto&) { registered = true; });
    require(registered, "child identity was not registered");
    int status = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (native_process_status(process, &status)) {
      require(std::chrono::steady_clock::now() < deadline, "exit observation deadline");
      std::this_thread::yield();
    }
    require(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 75,
            "owned exit classification was lost");
  }
  OwnedResourceIdentity foreign;
  foreign.stable_id = std::to_string(::getpid());
  foreign.secondary_id = "wrong-start-time";
  foreign.process_identity = executable.string() + "\n" + configuration_hash(executable);
  expect_failure([&] { (void)native_process_status(foreign); },
                 "live substituted process accepted during exit handling");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--exit-fixture") return 75;
  try {
    test_owned_container_services();
    test_retained_only();
    test_owned_process_exit(std::filesystem::canonical(argv[0]));
    test_growing_readiness_log();
    test_application_admission();
    test_round_trip_and_publication();
    test_scenario_recovery_records();
    test_process_inventory();
    test_network_extensions();
    test_passive_mirror_contract();
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
