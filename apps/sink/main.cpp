#include "../common.hpp"

int main() {
  demo::RuntimeTraceSink trace("sink");
  try {
    const auto config = graphx::load_config(demo::config_path());
    [[maybe_unused]] const auto& node = config.node("sink");
    graphx::TransportFactory transports;
    auto input =
        transports.create(config.edge("transformed"), graphx::ConnectionMode::listen, &trace);
    while (auto envelope = input->receive()) {
      std::cout << "sink seq=" << envelope->sequence << " value=" << envelope->payload
                << " trace=" << envelope->trace_id << std::endl;
    }
  } catch (const std::exception& error) {
    std::cerr << "sink: " << error.what() << '\n';
    return 1;
  }
}
