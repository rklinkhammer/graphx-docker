#include "config_test_support.hpp"
using namespace config_test;
namespace {
void runtime_projection() {
  const auto config = load_value(authored("static-route-policy"));
  const auto& network = config.network_infrastructure;
  expect(network.routers.size() == 4, "router and diagnostic namespaces share ownership");
  expect(network.attachments.size() == 9, "router attachments expanded once including mirrors");
  expect(network.routers.front().routes.size() == 1 &&
             !network.routers.front().routes.front().install_on_create,
         "deferred route remains manual");
  for (const auto& attachment : network.attachments) {
    if (attachment.id == "right-data")
      expect(attachment.aliases == std::vector<std::string>{"10.64.30.10/32"},
             "alias survives projection");
    expect(attachment.network_switch.starts_with("gxb"), "physical switch references are resolved");
  }
  expect(network.faults.empty(), "scenario actions are not baseline faults");
}
void logical_network_resolves() {
  const auto value = load_value(authored("qemu-node/tap")).resolved;
  const auto& network = value.at("network");
  expect(network.at("routers").array().size() == 1, "router expansion");
  std::size_t tagged{};
  for (const auto& a : network.at("attachments").array()) {
    if (a.at("kind") != graphx::ConfigValue("external"))
      expect(a.at("interface").text().size() <= 15, "bounded names");
    if (a.contains("vlan")) ++tagged;
  }
  expect(tagged == 3, "explicit router ports preserve VLANs");
  auto v = authored("macvlan");
  v["network"]["attachments"].array().front()["network"] = "missing";
  rejected(v, "E_REFERENCE", "network");
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

}  // namespace
int main() {
  return run_tests(
      {{"runtime network projection", runtime_projection},
       {"logical network resolution", logical_network_resolves},
       {"rollback reverse", infrastructure_transaction_rolls_back_in_reverse},
       {"preserve replacement", infrastructure_transaction_preserves_replacement},
       {"identity probe failure", infrastructure_transaction_rolls_back_identity_probe_failure}});
}
