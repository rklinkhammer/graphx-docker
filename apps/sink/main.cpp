#include "../common.hpp"

int main() {
  demo::install_signal_handlers();
  try {
    const auto config = graphx::load_config(demo::config_path());
    demo::RuntimeTraceSink trace("sink", config);
    [[maybe_unused]] const auto& node = config.node("sink");
    graphx::TransportFactory transports;
    auto input =
        transports.create(config.edge("transformed"), graphx::ConnectionMode::listen, &trace);
    const auto maximum = std::stoull(demo::env("GRAPHX_MAX_MESSAGES", "0"));
    std::uint64_t processed{};
    while (!demo::stopping() && (maximum == 0 || processed < maximum)) {
      trace.heartbeat();
      auto received = input->receive_result(std::chrono::milliseconds(200));
      if (received.status == graphx::ReceiveStatus::timeout) continue;
      if (received.status == graphx::ReceiveStatus::end_of_stream ||
          received.status == graphx::ReceiveStatus::cancelled)
        break;
      auto& envelope = received.envelope;
      const auto processing_start = std::chrono::steady_clock::now();
      std::cout << "sink seq=" << envelope->sequence << " value=" << envelope->payload
                << " trace=" << envelope->trace_id << std::endl;
      trace.on_processing("sink", *envelope,
                          std::chrono::steady_clock::now() - processing_start, true);
      ++processed;
    }
    input->close();
  } catch (const std::exception& error) {
    if (demo::stopping()) return 0;
    std::cerr << "sink: " << error.what() << '\n';
    return 1;
  }
}
