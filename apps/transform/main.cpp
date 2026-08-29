#include "../common.hpp"

int main() {
  demo::RuntimeTraceSink trace("transform");
  try {
    auto input = graphx::TcpTransport::listen(
        {demo::env("GRAPHX_INPUT_HOST", "0.0.0.0"), demo::port("GRAPHX_INPUT_PORT", 7001)},
        "samples", &trace);
    auto output = demo::connect_with_retry(
        {demo::env("GRAPHX_OUTPUT_HOST", "127.0.0.1"), demo::port("GRAPHX_OUTPUT_PORT", 7002)},
        "transformed", &trace);
    while (auto envelope = input.receive()) {
      const auto value = std::stoll(envelope->payload);
      envelope->type = "TransformedSample";
      envelope->payload = std::to_string(value * 2);
      envelope->attributes["operation"] = "multiply-by-two";
      output.send(*envelope);
    }
  } catch (const std::exception& error) {
    std::cerr << "transform: " << error.what() << '\n';
    return 1;
  }
}
