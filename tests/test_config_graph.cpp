#include "config_test_support.hpp"
#include "graphx/normalized_config.hpp"
using namespace config_test;
namespace {
void model_loads() {
  const auto config = load_value(authored());
  expect(config.version == 3 && config.id == "sample-pipeline", "v3 model");
  expect(config.nodes.size() == 3 && config.edges.size() == 2, "typed topology");
  expect(!config.node_types.empty(), "catalog types");
  expect(config.resolved.at("contract_version") == graphx::ConfigValue(2), "normalized v2");
  auto value = authored();
  value["nodes"]["generator"]["parameters"]["max_messages"] = 9;
  expect(
      load_value(value).resolved.at("nodes").array().front().at("parameters").at("max_messages") ==
          graphx::ConfigValue(9),
      "parameter resolution");
}
void strict_shape() {
  auto v = authored();
  v["version"] = 2;
  rejected(v, "E_VERSION", "version");
  v = authored();
  v["version"] = "3";
  rejected(v, "E_VERSION", "version");
  v = authored();
  v["unknown"] = true;
  rejected(v, "E_SCHEMA", "unknown");
  v = authored();
  v["nodes"]["generator"]["unknown"] = true;
  rejected(v, "E_SCHEMA", "unknown");
  v = authored();
  v["nodes"]["generator"]["parameters"]["unknown"] = 1;
  rejected(v, "E_PARAMETER", "unknown");
  v = authored();
  v["platform"]["history"]["enabled"] = "true";
  rejected(v, "E_SCHEMA", "enabled");
}
void yaml_boundary() {
  for (const auto source :
       {"version: 3\nversion: 3\n", "version: &v 3\ngraph: *v\n", "version: !!int 3\n",
        "version: 3\n---\nversion: 3\n", "version: [\n", "version: 3\n<<: {}\n"}) {
    TemporaryConfig file(source);
    try {
      (void)graphx::load_config(file.path());
      throw std::runtime_error("unsafe YAML accepted");
    } catch (const graphx::ConfigError&) {
    }
  }
  TemporaryConfig oversized(std::string(graphx::kMaxConfigBytes + 1, 'x'));
  try {
    (void)graphx::load_config(oversized.path());
    throw std::runtime_error("oversize accepted");
  } catch (const graphx::ConfigError&) {
  }
}
void ambient_environment_is_not_configuration() {
  const auto v = authored();
  ::setenv("GRAPHX_OVERRIDES", "version=2", 1);
  expect(load_value(v).version == 3, "ambient overrides ignored");
  ::unsetenv("GRAPHX_OVERRIDES");
  TemporaryConfig file(graphx::config_value_json(v));
  try {
    (void)graphx::load_config(file.path(), {{"version", "3"}});
    throw std::runtime_error("explicit override accepted");
  } catch (const graphx::ConfigError& e) {
    expect(e.diagnostics().front().code == "E_OVERRIDE", "explicit override diagnostic");
  }
}
}  // namespace
int main() {
  return run_tests({{"v3 typed model", model_loads},
                    {"closed shape", strict_shape},
                    {"YAML boundary", yaml_boundary},
                    {"no ambient interpreter", ambient_environment_is_not_configuration}});
}
