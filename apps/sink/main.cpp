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
      const auto processing_start = std::chrono::steady_clock::now();
      std::cout << "sink seq=" << envelope->sequence << " value=" << envelope->payload
                << " trace=" << envelope->trace_id << std::endl;
      trace.on_processing("sink", *envelope,
                          std::chrono::steady_clock::now() - processing_start, true);
    }
  } catch (const std::exception& error) {
    std::cerr << "sink: " << error.what() << '\n';
    return 1;
  }
}
