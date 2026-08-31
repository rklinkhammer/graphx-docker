#include "../common.hpp"

#include <limits>

int main() {
  demo::install_signal_handlers();
  try {
    const auto config = graphx::load_config(demo::config_path());
    demo::RuntimeTraceSink trace("generator", config);
    [[maybe_unused]] const auto& node = config.node("generator");
    graphx::TransportFactory transports;
    auto output = transports.create(config.edge("samples"), graphx::ConnectionMode::connect, &trace);
    const auto interval =
        std::chrono::milliseconds(std::stoi(demo::env("GRAPHX_INTERVAL_MS", "500")));
    const auto maximum = std::stoull(demo::env("GRAPHX_MAX_MESSAGES", "0"));
    for (std::uint64_t sequence = 1; !demo::stopping() && (maximum == 0 || sequence <= maximum);
         ++sequence) {
      while (!demo::stopping() && trace.paused()) {
        trace.heartbeat();
        demo::interruptible_pause(std::chrono::milliseconds(50));
      }
      if (demo::stopping()) break;
      trace.heartbeat();
      auto envelope = graphx::Envelope::make(sequence, "Sample", std::to_string(sequence));
      envelope.attributes["source"] = "generator";
      trace.on_processing("generator", envelope, std::chrono::nanoseconds{}, true);
      output->send(envelope);
      auto remaining = interval;
      while (!demo::stopping() && remaining.count() > 0) {
        const auto wait = std::min(remaining, std::chrono::milliseconds(50));
        std::this_thread::sleep_for(wait);
        remaining -= wait;
        trace.heartbeat();
      }
    }
    output->close();
  } catch (const std::exception& error) {
    if (demo::stopping()) return 0;
    std::cerr << "generator: " << error.what() << '\n';
    return 1;
  }
}
