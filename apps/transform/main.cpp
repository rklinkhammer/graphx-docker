#include "../common.hpp"

int main() {
  demo::RuntimeTraceSink trace("transform");
  try {
    const auto config = graphx::load_config(demo::config_path());
    [[maybe_unused]] const auto& node = config.node("transform");
    graphx::TransportFactory transports;
    auto input = transports.create(config.edge("samples"), graphx::ConnectionMode::listen, &trace);
    auto output = demo::connect_with_retry(transports, config.edge("transformed"), &trace);
    while (auto envelope = input->receive()) {
      const auto value = std::stoll(envelope->payload);
      envelope->type = "TransformedSample";
      envelope->payload = std::to_string(value * 2);
      envelope->attributes["operation"] = "multiply-by-two";
      output->send(*envelope);
    }
  } catch (const std::exception& error) {
    std::cerr << "transform: " << error.what() << '\n';
    return 1;
  }
}
