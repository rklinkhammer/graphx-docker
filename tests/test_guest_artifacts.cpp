#include "infra/qemu_resources.hpp"
#include "config_document.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <functional>
#include <unistd.h>

namespace {
using namespace graphx;
using namespace graphx::config_internal;
Value read(const std::filesystem::path& path) { return parse_document(read_document(path)); }
void write(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream stream(path);
  stream << bytes;
  if (!stream) throw std::runtime_error("fixture write failed");
}
void rejected(const std::function<void()>& action, const std::string& diagnostic = {}) {
  try {
    action();
  } catch (const std::exception& error) {
    if (!diagnostic.empty() && std::string(error.what()).find(diagnostic) == std::string::npos)
      throw std::runtime_error("wrong guest diagnostic: " + std::string(error.what()));
    return;
  }
  throw std::runtime_error("invalid guest artifact accepted");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("source root required");
    const auto source = std::filesystem::path(argv[1]);
    char temporary[] = "/tmp/graphx-guest-tests-XXXXXX";
    if (!::mkdtemp(temporary)) throw std::runtime_error("temporary directory failed");
    const auto root = std::filesystem::canonical(temporary);
    ExecutionOptions options;
    options.output = root / "compiled";
    options.release = root / "release";
    std::filesystem::create_directories(options.output / "guest-build");
    const auto artifacts = options.release / "guests/echo-x86";
    std::filesystem::create_directories(artifacts);
    const auto resolved = read(source / "tests/fixtures/compiled/qemu-tap/resolved.json");
    auto recipe = read(source / "config/catalog/guests/echo-x86.json");
    const auto node = *std::ranges::find_if(resolved.at("nodes").array(), [](const auto& value) {
      return value.at("execution").at("kind") == Value("qemu");
    });
    const auto publish = [&] {
      write(options.output / "guest-build/echo-x86.json", config_value_json(recipe));
      write(options.output / "qemu-plan.json",
            config_value_json(Object{
                {"version", 1},
                {"adapter", "qemu-process-adapter"},
                {"guests", Array{compiled_guest_plan(node, recipe, resolved.at("network"))}}}));
    };
    publish();
    const auto verify = [&] { infra::detail::verify_guest_artifacts(options, resolved); };
    rejected(verify);
    recipe["exists_today"] = true;
    Value manifest = Object{};
    for (const auto* key : {"id", "application", "architecture", "recipe_revision", "source_digest",
                            "source_date_epoch", "builder_image", "build_argv"})
      manifest[key] = recipe.at(key);
    manifest["guest_files"] = Object{};
    for (const auto* name :
         {"usr/bin/graphx", "usr/bin/graphx-packet-guest", "usr/lib/graphx/agent.py",
          "usr/lib/graphx/application", "etc/init.d/S80graphx", "usr/lib/graphx/sdr/radio.py",
          "usr/lib/graphx/sdr/sdr_simulator.py"})
      manifest["guest_files"][name] = std::string(64, 'a');
    manifest["guest_files"]["usr/lib/graphx/application"] =
        sha256(recipe.at("application").text() + "\n");
    write(artifacts / "artifact-manifest.json", config_value_json(manifest));
    // Synthetic bytes exercise verifier boundaries; no guest boot is inferred.
    for (const auto* file : {"bzImage", "rootfs.cpio.gz", "licenses.json"})
      write(artifacts / file, "synthetic-test-only\n");
    recipe["outputs"] = Array{};
    for (const auto* file :
         {"bzImage", "rootfs.cpio.gz", "licenses.json", "artifact-manifest.json"})
      recipe["outputs"].array().emplace_back(
          Object{{"path", file}, {"sha256", infra::detail::configuration_hash(artifacts / file)}});
    publish();
    verify();
    write(artifacts / "bzImage", "tampered");
    rejected(verify);
    write(artifacts / "bzImage", "synthetic-test-only\n");
    std::filesystem::rename(artifacts / "bzImage", root / "kernel");
    rejected(verify, "E_GUEST_UNAVAILABLE");
    std::filesystem::create_symlink(root / "kernel", artifacts / "bzImage");
    rejected(verify);
    std::filesystem::remove(artifacts / "bzImage");
    std::filesystem::rename(root / "kernel", artifacts / "bzImage");
    write(artifacts / "unexpected", "unexpected");
    rejected(verify);
    std::filesystem::remove(artifacts / "unexpected");
    const auto valid = recipe;
    recipe["architecture"] = "arm64";
    write(options.output / "guest-build/echo-x86.json", config_value_json(recipe));
    rejected(verify);
    recipe = valid;
    recipe["source_digest"] = std::string(64, 'a');
    publish();
    rejected(verify);
    recipe = valid;
    publish();
    auto plan = read(options.output / "qemu-plan.json");
    plan["guests"].array()[0]["argv"].array().emplace_back("-netdev");
    write(options.output / "qemu-plan.json", config_value_json(plan));
    rejected(verify);
    std::filesystem::remove_all(root);
    std::cout << "Guest artifact availability, checksum, architecture, provenance, path and plan "
                 "boundaries passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
