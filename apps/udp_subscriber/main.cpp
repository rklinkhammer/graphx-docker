#include "../common.hpp"

int main() {
  demo::install_signal_handlers();
  try {
    const auto config = graphx::load_config(demo::config_path());
    const auto edge_id = demo::env("GRAPHX_EDGE", "messages");
    const auto node_id = demo::env("GRAPHX_NODE", "subscriber");
    demo::RuntimeTraceSink trace(node_id, config);
    graphx::TransportFactory transports;
    auto input = transports.create(config.edge(edge_id), graphx::ConnectionMode::listen, &trace);
    const auto maximum = demo::unsigned_env("GRAPHX_MAX_MESSAGES", 5, 1, 1'000'000);
    const auto timeout =
        std::chrono::milliseconds(demo::unsigned_env("GRAPHX_TIMEOUT_MS", 5000, 1, 600'000));
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::uint64_t received{};
    while (received < maximum && !demo::stopping()) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) break;
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      const auto result =
          input->receive_result(std::min(remaining, std::chrono::milliseconds(200)));
      if (result.status == graphx::ReceiveStatus::timeout) continue;
      if (result.status == graphx::ReceiveStatus::cancelled) break;
      if (result.status == graphx::ReceiveStatus::end_of_stream)
        throw std::runtime_error("UDP transport returned unexpected end_of_stream");
      ++received;
      std::cout << "received seq=" << result.envelope->sequence
                << " payload=" << result.envelope->payload << '\n';
    }
    input->close();
    if (demo::stopping()) return 130;
    if (received != maximum) {
      std::cerr << "udp-subscriber: timed out after " << received << " of " << maximum
                << " messages\n";
      return 2;
    }
    std::cout << "PASS received=" << received << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "udp-subscriber: " << error.what() << '\n';
    return 1;
  }
}
