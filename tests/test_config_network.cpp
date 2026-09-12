#include "config_test_support.hpp"

using namespace std::chrono_literals;
using namespace config_test;

namespace {

void current_profiles_and_attachments_load() {
  TemporaryConfig file(current_network_config());
  const auto config = graphx::load_config(file.path());
  expect(config.version == 2, "current configuration model");
  const auto& network = config.network_infrastructure.network("semantic-lan");
  expect(network.profile == graphx::NetworkProfile::ethernet && network.uplink.empty(),
         "semantic profile model");
  expect(config.network_infrastructure.attachments.size() == 2 &&
             config.network_infrastructure.attachments.front().kind ==
                 graphx::AttachmentKind::external,
         "current attachment model");

  const struct ProfileCase {
    graphx::NetworkProfile profile{graphx::NetworkProfile::ethernet};
    std::string_view name;
    std::string_view mac_identity;
    std::string_view learning;
    std::string_view filtering;
    std::string_view arp;
    std::string_view broadcast;
    std::string_view multicast;
    std::string_view routing;
    std::string_view isolation;
    std::string_view management;
  } profiles[] = {
      {graphx::NetworkProfile::ethernet, "ethernet", "endpoint", "dynamic", "ovs", "endpoint",
       "flood", "flood", "l2", "none", "separate"},
      {graphx::NetworkProfile::macvlan, "macvlan", "endpoint", "dynamic", "ovs", "endpoint",
       "flood", "flood", "l2", "host", "separate"},
      {graphx::NetworkProfile::ipvlan_l2, "ipvlan-l2", "shared-uplink", "suppressed", "ovs",
       "endpoint-shared-mac", "flood", "flood", "l2", "host", "separate"},
      {graphx::NetworkProfile::ipvlan_l3, "ipvlan-l3", "shared-uplink", "none", "route",
       "suppressed", "suppressed", "suppressed", "l3", "endpoint", "separate"},
      {graphx::NetworkProfile::ipvlan_l3s, "ipvlan-l3s", "shared-uplink", "none",
       "source-validated", "suppressed", "suppressed", "suppressed", "l3-source-validated",
       "endpoint", "separate"},
  };
  for (const auto& profile : profiles) {
    expect(graphx::to_string(profile.profile) == profile.name, "profile name");
    const auto& semantics = graphx::profile_semantics(profile.profile);
    expect(semantics.mac_identity == profile.mac_identity &&
               semantics.learning == profile.learning && semantics.filtering == profile.filtering &&
               semantics.arp == profile.arp && semantics.broadcast == profile.broadcast &&
               semantics.multicast == profile.multicast && semantics.routing == profile.routing &&
               semantics.isolation == profile.isolation &&
               semantics.management == profile.management,
           "exact profile behavior");
  }
}

void mixed_network_model_and_plan_load() {
  const auto config = graphx::load_config(std::filesystem::path(GRAPHX_SOURCE_DIR) /
                                          "examples/mixed-network/graphx.yaml");
  expect(config.version == 2 && config.network_infrastructure.network("gx-mac-domain").profile ==
                                    graphx::NetworkProfile::macvlan,
         "canonical macvlan semantic profile");
  expect(config.network_infrastructure.network("gx-ipv-domain").profile ==
             graphx::NetworkProfile::ipvlan_l2,
         "canonical ipvlan L2 semantic profile");
  expect(config.network_infrastructure.router("domain-router").interfaces.size() == 2,
         "router interfaces");
  expect(config.network_infrastructure.network_switch("br-gx-mac").mirror.has_value(),
         "OVS mirror model");
  expect(config.network_infrastructure.attachments.size() == 7,
         "canonical mixed network attachment model");
}

void standalone_network_examples_load() {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples";
  const auto macvlan = graphx::load_config(root / "macvlan/graphx.yaml");
  expect(macvlan.version == 2 && macvlan.network_infrastructure.networks.size() == 1,
         "standalone macvlan semantic domain");
  expect(macvlan.network_infrastructure.attachments.size() == 3,
         "standalone macvlan OVS attachments");

  const auto layer_two = graphx::load_config(root / "ipvlan-l2/graphx.yaml");
  expect(layer_two.version == 2 && layer_two.network_infrastructure.networks.size() == 3,
         "one IPvlan L2 semantic domain per node");
  expect(layer_two.network_infrastructure.routers.size() == 1 &&
             layer_two.network_infrastructure.switches.size() == 3,
         "IPvlan L2 routed domains");

  const auto layer_three = graphx::load_config(root / "ipvlan-l3/graphx.yaml");
  expect(layer_three.network_infrastructure.networks.size() == 1 &&
             layer_three.network_infrastructure.networks.front().subnets.size() == 3,
         "one supported multi-subnet IPvlan L3 semantic network");
  expect(layer_three.network_infrastructure.attachments.size() == 3, "IPvlan L3 OVS attachments");
}

void static_route_policy_model_and_plan_load() {
  const auto path =
      std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples/static-route-policy/graphx.yaml";
  const auto config = graphx::load_config(path);
  const auto& router = config.network_infrastructure.router("route-router");
  expect(config.network_infrastructure.networks.size() == 3, "route lab domains");
  expect(config.network_infrastructure.switches.size() == 3, "route lab OVS switches");
  expect(router.interfaces.size() == 3 && router.routes.size() == 1 && router.policies.size() == 3,
         "route lab router model");
  expect(!router.routes.front().install_on_create, "manual route model");
  expect(config.network_infrastructure.edge_paths.size() == 3, "route lab ordered edge paths");
  expect(config.version == 2, "canonical route lab uses the current configuration");
  const auto apply = graphx::route_command(config, "route-router", "10.64.30.10/32", false);
  expect(graphx::format_command(apply) ==
             "ip netns exec gx-route-router ip route replace 10.64.30.10/32 via 10.64.3.10 dev "
             "rt-right",
         "manual route apply command");
  const auto clear = graphx::route_command(config, "route-router", "10.64.30.10/32", true);
  expect(clear.ignore_failure &&
             graphx::format_command(clear).find("route delete 10.64.30.10/32") != std::string::npos,
         "manual route clear command");
}

std::vector<std::string> file_identity_command(const std::filesystem::path& path) {
  return {"python3", "-c",
          "import os,sys; value=os.stat(sys.argv[1]); "
          "print(f'{value.st_ino}:{value.st_mode}')",
          path.string()};
}

void infrastructure_transaction_rolls_back_in_reverse() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("graphx-infra-transaction-" + std::to_string(::getpid()));
  const auto first = directory / "first";
  const auto second = directory / "second";
  std::filesystem::create_directories(directory);
  const std::vector<graphx::InfraCommand> commands = {
      {{"touch", first.string()}, {}, false, {"rm", first.string()}, file_identity_command(first)},
      {{"touch", second.string()},
       {},
       false,
       {"rm", second.string()},
       file_identity_command(second)},
      {{"false"}, {}, false, {}, {}},
  };
  std::ostringstream output, errors;
  const auto status = graphx::execute_infrastructure_plan(commands, false, output, errors);
  expect(status != 0, "transaction reports command failure");
  expect(!std::filesystem::exists(first) && !std::filesystem::exists(second),
         "transaction removes completed resources");
  const auto log = output.str();
  expect(log.find("- rm " + second.string()) < log.find("- rm " + first.string()),
         "transaction rolls back in reverse order");
  std::filesystem::remove_all(directory);
}

void infrastructure_transaction_preserves_replacement() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("graphx-infra-replacement-" + std::to_string(::getpid()));
  const auto resource = directory / "resource";
  std::filesystem::create_directories(directory);
  const std::vector<graphx::InfraCommand> commands = {
      {{"touch", resource.string()},
       {},
       false,
       {"rm", "-rf", resource.string()},
       file_identity_command(resource)},
      {{"sh", "-c",
        "rm '" + resource.string() + "' && mkdir '" + resource.string() + "' && exit 23"},
       {},
       false,
       {},
       {}},
  };
  std::ostringstream output, errors;
  const auto status = graphx::execute_infrastructure_plan(commands, false, output, errors);
  expect(status == 23, "replacement transaction reports injected failure");
  expect(std::filesystem::is_directory(resource), "transaction preserves replacement resource");
  expect(output.str().find("rollback skipped; resource identity changed") != std::string::npos,
         "transaction reports identity-safe rollback skip");
  std::filesystem::remove_all(directory);
}

void infrastructure_transaction_rolls_back_identity_probe_failure() {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("graphx-infra-identity-failure-" + std::to_string(::getpid()));
  const auto first = directory / "first";
  const auto second = directory / "second";
  std::filesystem::create_directories(directory);
  const std::vector<graphx::InfraCommand> commands = {
      {{"touch", first.string()}, {}, false, {"rm", first.string()}, file_identity_command(first)},
      {{"touch", second.string()}, {}, false, {"rm", second.string()}, {"sh", "-c", "exit 23"}},
  };
  std::ostringstream output, errors;
  const auto status = graphx::execute_infrastructure_plan(commands, false, output, errors);
  expect(status == 23, "identity failure reports probe status");
  expect(!std::filesystem::exists(first) && !std::filesystem::exists(second),
         "identity failure removes current and completed resources");
  const auto log = output.str();
  expect(log.find("- rm " + second.string()) < log.find("- rm " + first.string()),
         "identity failure rolls back current resource first");
  std::filesystem::remove_all(directory);
}

void invalid_network_reference_is_rejected() {
  TemporaryConfig file(std::string(valid_config) + R"yaml(
network:
  networks:
    - { id: lab, profile: ethernet, subnets: [10.0.0.0/24], gateway: 10.0.0.1 }
  attachments:
    - { id: source-data, kind: external, owner: source, network: missing, address: 10.0.0.10/24 }
  edge_paths:
    sample-edge: [source, missing, target]
)yaml");
  try {
    [[maybe_unused]] const auto ignored = graphx::load_config(file.path());
    throw std::runtime_error("invalid network reference was accepted");
  } catch (const graphx::ConfigError& error) {
    expect(diagnostic_contains(error, "unknown network 'missing'"), "unknown network diagnostic");
    expect(diagnostic_contains(error, "unknown hop 'missing'"), "unknown hop diagnostic");
  }
}

}  // namespace

int main() {
  return run_tests({
      {"current profiles and attachments", current_profiles_and_attachments_load},
      {"mixed network model", mixed_network_model_and_plan_load},
      {"standalone network examples", standalone_network_examples_load},
      {"static route policy model", static_route_policy_model_and_plan_load},
      {"infrastructure transaction rollback", infrastructure_transaction_rolls_back_in_reverse},
      {"infrastructure transaction replacement", infrastructure_transaction_preserves_replacement},
      {"infrastructure transaction identity failure",
       infrastructure_transaction_rolls_back_identity_probe_failure},
      {"invalid network reference", invalid_network_reference_is_rejected},
  });
}
