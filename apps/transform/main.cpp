#include "../common.hpp"

int main() {
  demo::install_signal_handlers();
  try {
    const auto config = graphx::load_config(demo::config_path());
    const auto node_id = demo::selected_node(config, "transform");
    demo::RuntimeTraceSink trace(node_id, config);
    [[maybe_unused]] const auto& node = config.node(node_id);
    graphx::TransportFactory transports;
    auto input = transports.create(demo::selected_edge(config, node_id, false, "samples"),
                                   graphx::ConnectionMode::listen, &trace);
    auto output = transports.create(demo::selected_edge(config, node_id, true, "transformed"),
                                    graphx::ConnectionMode::connect, &trace);
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
      const auto value = std::stoll(envelope->payload);
      envelope->type = "TransformedSample";
      envelope->payload = std::to_string(value * 2);
      envelope->attributes["operation"] = "multiply-by-two";
      trace.on_processing(node_id, *envelope, std::chrono::steady_clock::now() - processing_start,
                          true);
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
