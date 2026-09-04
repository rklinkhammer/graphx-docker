#include "../common.hpp"

int main() {
  demo::install_signal_handlers();
  try {
    const auto config = graphx::load_config(demo::config_path());
    const auto edge_id = demo::env("GRAPHX_EDGE", "messages");
    const auto node_id = demo::env("GRAPHX_NODE", "publisher");
    demo::RuntimeTraceSink trace(node_id, config);
    graphx::TransportFactory transports;
    auto output = transports.create(config.edge(edge_id), graphx::ConnectionMode::connect, &trace);
    const auto maximum = demo::unsigned_env("GRAPHX_MAX_MESSAGES", 5, 1, 1'000'000);
    const auto interval =
        std::chrono::milliseconds(demo::unsigned_env("GRAPHX_INTERVAL_MS", 100, 0, 600'000));
    const auto start_delay =
        std::chrono::milliseconds(demo::unsigned_env("GRAPHX_START_DELAY_MS", 0, 0, 600'000));
    const auto prefix = demo::env("GRAPHX_MESSAGE_PREFIX", "message");
    demo::interruptible_pause(start_delay);
    for (std::uint64_t sequence = 1; sequence <= maximum && !demo::stopping(); ++sequence) {
      auto envelope =
          graphx::Envelope::make(sequence, "UdpExample", prefix + "-" + std::to_string(sequence));
      envelope.attributes["source"] = node_id;
      output->send(envelope);
      std::cout << "published seq=" << sequence << " payload=" << envelope.payload << '\n';
      if (sequence != maximum) demo::interruptible_pause(interval);
    }
    output->close();
    return demo::stopping() ? 130 : 0;
  } catch (const std::exception& error) {
    std::cerr << "udp-publisher: " << error.what() << '\n';
    return 1;
  }
}
