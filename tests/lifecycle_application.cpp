// Deliberately controllable application for testing the production owned lifecycle.
#include "graphx/node_settings.hpp"
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
}  // namespace
int main(int argc, char** argv) try {
  const auto args = graphx::node_arguments(argc, argv);
  const auto node = graphx::load_node_settings(args.config, args.node);
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  std::string mode;
  std::ifstream(args.config.parent_path().parent_path().parent_path() / (args.node + ".fault")) >>
      mode;
  if (mode == "unavailable") return 75;
  if (mode == "invalid") return 78;
  if (mode == "unknown") return 1;
  if (mode == "signal") std::raise(SIGKILL);
  if (mode != "timeout" && !graphx::await_node_release(node, args, [] { return stopped != 0; }))
    return 0;
  if (mode != "timeout") std::cout << "released node=" << node.id() << std::endl;
  while (!stopped) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 78;
}
