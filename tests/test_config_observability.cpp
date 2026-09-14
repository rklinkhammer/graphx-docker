#include "config_test_support.hpp"
using namespace config_test;
namespace {
void bounded_platform_defaults() {
  const auto p = load_value(authored()).resolved.at("platform");
  expect(p.at("history").at("enabled").boolean(), "history defaults enabled");
  expect(p.at("history").at("max_database_bytes").integer() == 268435456, "bounded database");
  for (const auto key : {"max_records", "queue_capacity", "query_timeout_ms"}) {
    auto v = authored();
    v["platform"]["history"][key] = 0;
    rejected(v, "E_SCHEMA", key);
  }
  auto v = authored();
  v["platform"]["capture"]["provider"] = "arbitrary";
  rejected(v, "E_SCHEMA", "provider");
  v = authored();
  v["platform"]["history"]["database_file"] = "/tmp/secret";
  rejected(v, "E_SCHEMA", "database_file");
}
void secrets_are_references() {
  auto v = authored("sdr-node/simulated");
  const auto resolved = graphx::config_value_json(load_value(v).resolved);
  expect(resolved.find("file:") == std::string::npos,
         "no credential source paths in resolved output");
  v["credentials"]["bad"] = graphx::ConfigValue::Object{{"value", "SECRET-MUST-NOT-APPEAR"}};
  try {
    (void)load_value(v);
    throw std::runtime_error("inline secret accepted");
  } catch (const graphx::ConfigError& e) {
    expect(std::string(e.what()).find("SECRET-MUST-NOT-APPEAR") == std::string::npos,
           "no secret in diagnostics");
  }
}
void serial_grants_are_qemu_scoped() {
  using Value = graphx::ConfigValue;
  auto v = authored("qemu-node/tap");
  v["credentials"]["console-operator"] = Value::Object{
      {"identity", "operator"}, {"provider", "external"}, {"members", Value::Array{"token"}}};
  v["platform"]["control"] =
      Value::Object{{"enabled", true},
                    {"grants", Value::Array{Value::Object{{"credential", "console-operator"},
                                                          {"nodes", Value::Array{"qemu-node"}},
                                                          {"actions", Value::Array{"serial"}}}}}};
  expect(load_value(v).resolved.at("platform").at("control").at("grants").array().size() == 1,
         "QEMU serial grant preserved");
  v["platform"]["control"]["grants"].array()[0]["nodes"] = Value::Array{"host-peer"};
  rejected(v, "E_REFERENCE", "platform.control.grants");
  v["platform"]["control"]["grants"].array()[0]["nodes"] = Value::Array{"collector"};
  rejected(v, "E_REFERENCE", "platform.control.grants");
}
}  // namespace
int main() {
  return run_tests({{"bounded platform defaults", bounded_platform_defaults},
                    {"credential references", secrets_are_references},
                    {"serial grants", serial_grants_are_qemu_scoped}});
}
