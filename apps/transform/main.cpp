#include "../common.hpp"

int main() {
  demo::RuntimeTraceSink trace("transform");
  try {
    const auto config = graphx::load_config(demo::config_path());
    [[maybe_unused]] const auto& node = config.node("transform");
    graphx::TransportFactory transports;
    auto input = transports.create(config.edge("samples"), graphx::ConnectionMode::listen, &trace);
    auto output =
        transports.create(config.edge("transformed"), graphx::ConnectionMode::connect, &trace);
    while (auto envelope = input->receive()) {
      const auto processing_start = std::chrono::steady_clock::now();
      const auto value = std::stoll(envelope->payload);
      envelope->type = "TransformedSample";
      envelope->payload = std::to_string(value * 2);
      envelope->attributes["operation"] = "multiply-by-two";
      trace.on_processing("transform", *envelope,
                          std::chrono::steady_clock::now() - processing_start, true);
      output->send(*envelope);
    }
  } catch (const std::exception& error) {
    std::cerr << "transform: " << error.what() << '\n';
    return 1;
  }
}
