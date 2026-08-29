#include "../common.hpp"

#include <csignal>
#include <limits>

namespace {
volatile std::sig_atomic_t running = 1;
}

int main() {
  std::signal(SIGINT, [](int) { running = 0; });
  std::signal(SIGTERM, [](int) { running = 0; });
  demo::RuntimeTraceSink trace("generator");
  try {
    const auto config = graphx::load_config(demo::config_path());
    [[maybe_unused]] const auto& node = config.node("generator");
    graphx::TransportFactory transports;
    auto output = transports.create(config.edge("samples"), graphx::ConnectionMode::connect, &trace);
    const auto interval =
        std::chrono::milliseconds(std::stoi(demo::env("GRAPHX_INTERVAL_MS", "500")));
    const auto maximum = std::stoull(demo::env("GRAPHX_MAX_MESSAGES", "0"));
    for (std::uint64_t sequence = 1; running && (maximum == 0 || sequence <= maximum);
         ++sequence) {
      auto envelope = graphx::Envelope::make(sequence, "Sample", std::to_string(sequence));
      envelope.attributes["source"] = "generator";
      output->send(envelope);
      std::this_thread::sleep_for(interval);
    }
  } catch (const std::exception& error) {
    std::cerr << "generator: " << error.what() << '\n';
    return 1;
  }
}
