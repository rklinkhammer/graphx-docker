#include "../common.hpp"

int main() {
  demo::install_signal_handlers();
  try {
    const auto config = graphx::load_config(demo::config_path());
    demo::RuntimeTraceSink trace("transform", config);
    [[maybe_unused]] const auto& node = config.node("transform");
    graphx::TransportFactory transports;
    auto input = transports.create(config.edge("samples"), graphx::ConnectionMode::listen, &trace);
    auto output =
        transports.create(config.edge("transformed"), graphx::ConnectionMode::connect, &trace);
    const auto maximum = std::stoull(demo::env("GRAPHX_MAX_MESSAGES", "0"));
    std::uint64_t processed{};
    while (!demo::stopping() && (maximum == 0 || processed < maximum)) {
      trace.heartbeat();
      auto envelope = input->receive(std::chrono::milliseconds(200));
      if (!envelope) continue;
      const auto processing_start = std::chrono::steady_clock::now();
      const auto value = std::stoll(envelope->payload);
      envelope->type = "TransformedSample";
      envelope->payload = std::to_string(value * 2);
      envelope->attributes["operation"] = "multiply-by-two";
      trace.on_processing("transform", *envelope,
                          std::chrono::steady_clock::now() - processing_start, true);
      output->send(*envelope);
      ++processed;
    }
    output->close();
    input->close();
  } catch (const std::exception& error) {
    if (demo::stopping()) return 0;
    std::cerr << "transform: " << error.what() << '\n';
    return 1;
  }
}
