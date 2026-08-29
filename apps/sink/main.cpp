#include "../common.hpp"

int main() {
  demo::RuntimeTraceSink trace("sink");
  try {
    auto input = graphx::TcpTransport::listen(
        {demo::env("GRAPHX_INPUT_HOST", "0.0.0.0"), demo::port("GRAPHX_INPUT_PORT", 7002)},
        "transformed", &trace);
    while (auto envelope = input.receive()) {
      std::cout << "sink seq=" << envelope->sequence << " value=" << envelope->payload
                << " trace=" << envelope->trace_id << std::endl;
    }
  } catch (const std::exception& error) {
    std::cerr << "sink: " << error.what() << '\n';
    return 1;
  }
}
