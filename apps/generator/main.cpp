#include "../common.hpp"

#include <csignal>
#include <limits>

namespace { volatile std::sig_atomic_t running = 1; }

int main() {
  std::signal(SIGINT, [](int) { running = 0; });
  std::signal(SIGTERM, [](int) { running = 0; });
  demo::RuntimeTraceSink trace("generator");
  try {
    auto output = demo::connect_with_retry(
        {demo::env("GRAPHX_OUTPUT_HOST", "127.0.0.1"),
         demo::port("GRAPHX_OUTPUT_PORT", 7001)},
        "samples", &trace);
    const auto interval = std::chrono::milliseconds(std::stoi(demo::env("GRAPHX_INTERVAL_MS", "500")));
    for (std::uint64_t sequence = 1; running; ++sequence) {
      auto envelope = graphx::Envelope::make(sequence, "Sample", std::to_string(sequence));
      envelope.attributes["source"] = "generator";
      output.send(envelope);
      std::this_thread::sleep_for(interval);
    }
  } catch (const std::exception& error) {
    std::cerr << "generator: " << error.what() << '\n';
    return 1;
  }
}
