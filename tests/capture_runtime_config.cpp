#include "../apps/common.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Operation>
void expect_failure(Operation&& operation, std::string_view expected) {
  try {
    operation();
  } catch (const std::exception& error) {
    expect(std::string_view(error.what()).find(expected) != std::string_view::npos,
           "runtime capture diagnostic");
    return;
  }
  throw std::runtime_error("invalid runtime capture value was accepted");
}

}  // namespace

int main() {
  try {
    ::setenv("GRAPHX_CAPTURE_ENABLED", "TrUe", 1);
    expect(demo::boolean_env("GRAPHX_CAPTURE_ENABLED", false), "mixed-case true environment");
    ::setenv("GRAPHX_CAPTURE_ENABLED", "OFF", 1);
    expect(!demo::boolean_env("GRAPHX_CAPTURE_ENABLED", true), "mixed-case false environment");
    ::setenv("GRAPHX_CAPTURE_ENABLED", "maybe", 1);
    expect_failure([] { static_cast<void>(demo::boolean_env("GRAPHX_CAPTURE_ENABLED", false)); },
                   "must be one of");

    ::setenv("GRAPHX_CAPTURE_PROVIDER", "pcapng", 1);
    expect(demo::capture_provider_env("") == "pcapng", "supported capture provider");
    ::setenv("GRAPHX_CAPTURE_PROVIDER", "ovs-span", 1);
    expect(demo::capture_provider_env("") == "ovs-span", "supported OVS capture provider");
    ::setenv("GRAPHX_CAPTURE_PROVIDER", "pcapgn", 1);
    expect_failure([] { static_cast<void>(demo::capture_provider_env("pcapng")); },
                   "must be 'pcapng' or 'ovs-span'");
    ::unsetenv("GRAPHX_CAPTURE_ENABLED");
    ::unsetenv("GRAPHX_CAPTURE_PROVIDER");
    std::cout << "GraphX runtime capture configuration validation passed\n";
    return 0;
  } catch (const std::exception& error) {
    ::unsetenv("GRAPHX_CAPTURE_ENABLED");
    ::unsetenv("GRAPHX_CAPTURE_PROVIDER");
    std::cerr << error.what() << '\n';
    return 1;
  }
}
