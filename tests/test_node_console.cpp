#include "infra/node_console.hpp"
#include "infra/ownership_state.hpp"
#include "infra/process_resources.hpp"
#include "config_document.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
using namespace graphx::infra::detail;
using Value = graphx::ConfigValue;
int run(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--relay") {
    graphx::ExecutionOptions options;
    options.state_root = argv[2];
    options.output = options.state_root / "compiled";
    options.owner_token = argv[3];
    return execute_node_console(
        options,
        Value::Object{{"graph_id", "test"},
                      {"nodes", Value::Array{Value::Object{
                                    {"node_id", "worker"},
                                    {"execution", Value::Object{{"kind", "container"}}}}}}});
  }
  char pattern[] = "/tmp/graphx-console-XXXXXX";
  const char* created = ::mkdtemp(pattern);
  if (!created) return 1;
  const auto root = std::filesystem::canonical(created);
  graphx::OwnedResourceIdentity relay;
  try {
    const auto state_root = root / "test";
    ensure_state_root(state_root);
    OwnershipState state;
    state.graph_id = "test";
    state.owner_token = random_token();
    state.config_hash = std::string(64, 'a');
    state.status = "creating";
    prepare_node_console(state_root, state);
    const auto initial = state.console_name;
    const auto restored = load_state(state_root / "ownership.yml");
    if (restored.console_name != initial || restored.console_inode != state.console_inode)
      throw std::runtime_error("console identity not persisted");
    std::ofstream(state_root / initial / "test.json") << "{}";
    prepare_node_console(state_root, state);
    if (initial == state.console_name || std::filesystem::exists(state_root / initial))
      throw std::runtime_error("console generation was reused");
    const auto directory = state_root / state.console_name;
    std::filesystem::create_symlink("/etc/passwd", directory / "test.json");
    bool refused = false;
    try {
      prepare_node_console(state_root, state);
    } catch (const std::runtime_error&) {
      refused = true;
    }
    if (!refused || !std::filesystem::is_symlink(directory / "test.json"))
      throw std::runtime_error("unexpected file was not retained");
    std::filesystem::remove(directory / "test.json");
    std::filesystem::create_directory(root / "compiled");
    std::ofstream(root / "compiled/compile-manifest.json") << "{}";
    state.config_hash = configuration_hash(root / "compiled/compile-manifest.json");
    state.status = "ready";
    graphx::OwnedResourceIdentity container;
    container.kind = "container";
    container.name = "graphx-test-worker";
    container.stable_id = "container-id";
    container.secondary_id = "image-id";
    container.process_identity = state.owner_token;
    state.processes.push_back(container);
    const auto inspect = root / "inspect.json";
    auto publish_inspect = [&](const std::string& owner) {
      Value::Object labels{{"org.graphx.graph", "test"}, {"org.graphx.owner", owner}};
      Value::Object object{{"Id", "container-id"},
                           {"Image", "image-id"},
                           {"State", Value::Object{{"Running", true}}},
                           {"Config", Value::Object{{"Labels", labels}}}};
      publish_execution_file(inspect, graphx::config_value_json(Value::Array{object}), 0600);
    };
    publish_inspect(state.owner_token);
    const auto docker = root / "docker";
    std::ofstream(docker)
        << "#!/bin/sh\ncase \"$1\" in\ninspect) [ \"$2\" = container-id ] || exit 2; cat "
           "\"$CONSOLE_TEST_INSPECT\";;\nlogs) [ \"$4\" = container-id ] || exit 2; printf "
           "'container output\\n';;\n*) exit 3;;\nesac\n";
    ::chmod(docker.c_str(), 0700);
    NativeProcessOptions start;
    start.id = "mg-node-console";
    start.executable = std::filesystem::canonical(argv[0]);
    start.cwd = root;
    start.log = directory / "relay.log";
    start.argv = {start.executable.string(), "--relay", root.string(), state.owner_token};
    start.environment = {{"PATH", root.string() + ":/usr/bin:/bin"},
                         {"CONSOLE_TEST_INSPECT", inspect.string()}};
    relay = start_native_process(start, [&](const auto& process) {
      state.processes.push_back(process);
      save_state(state_root / "ownership.yml", state);
    });
    auto await_status = [&](const std::string& expected) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = directory / "worker.json";
        if (std::filesystem::exists(snapshot)) {
          const auto value = graphx::config_internal::parse_document(
              graphx::config_internal::read_document(snapshot, 262144));
          if (value.at("status") == Value(expected)) {
            if (expected == "running" &&
                value.at("hex") != Value("636f6e7461696e6572206f75747075740a"))
              throw std::runtime_error("log bytes changed");
            return;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      throw std::runtime_error("console relay status timeout: " + expected);
    };
    await_status("running");
    publish_inspect("wrong-owner");
    await_status("unavailable");
    stop_native_process(relay);
    relay.stable_id.clear();
    std::filesystem::remove_all(root);
    std::cout
        << "Console identity, retention, owned container logs and replacement refusal passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (!relay.stable_id.empty()) try {
        stop_native_process(relay);
      } catch (...) {
        return 1;
      }
    std::filesystem::remove_all(root);
    return 1;
  }
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
