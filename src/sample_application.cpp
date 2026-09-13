#include "graphx/sample_application.hpp"
#include "graphx/node_settings.hpp"
#include "application_observer.hpp"

#include <limits>
#include <mutex>

namespace graphx {
int run_sample_application(int argc, char** argv, std::string_view type) {
  demo::install_signal_handlers();
  try {
    const auto arguments = node_arguments(argc, argv);
    const auto settings = load_node_settings(arguments.config, arguments.node, type);
    const auto& node = settings.resolved;
    const auto execution = node.at("execution").at("kind").text();
    if (execution != "native" && execution != "container" && execution != "namespace")
      throw std::runtime_error("E_EXECUTION: this executable requires a managed process instance");
    for (const auto& [name, peers] : node.at("bindings").object()) {
      (void)name;
      for (const auto& peer : peers.array())
        if (peer.at("security").at("profile") != ConfigValue("none"))
          throw std::runtime_error(
              "E_PHASE_UNAVAILABLE: GraphX TLS credential staging requires P5");
    }
    const auto telemetry_secret = demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET");
    if (!node.at("telemetry").at("credential").is_null() && telemetry_secret.empty())
      throw std::runtime_error(
          "E_PHASE_UNAVAILABLE: telemetry credential staging requires P5 or an explicit runtime "
          "secret");
    GraphConfig observer;
    observer.observability.metrics.exporters = {"console"};
    observer.observability.tracing.exporters = {"console"};
    // Credential staging and graph-level platform configuration belong to P5.
    // Explicit runtime credentials enable the existing authenticated exporter.
    if (!node.at("telemetry").at("credential").is_null()) {
      observer.observability.metrics.exporters.push_back("udp-json");
      observer.observability.telemetry.host = node.at("telemetry").at("host").text();
      observer.observability.telemetry.port =
          static_cast<std::uint16_t>(node.at("telemetry").at("port").integer());
    }
    const auto& capture = node.at("capture");
    observer.observability.capture.enabled =
        capture.at("enabled").boolean() && capture.at("provider") == ConfigValue("application");
    observer.observability.capture.provider = "pcapng";
    observer.observability.capture.directory = capture.at("directory").text();
    observer.observability.capture.snaplen =
        capture.contains("snaplen") ? static_cast<std::uint32_t>(capture.at("snaplen").integer())
                                    : 65535;
    observer.observability.capture.max_file_bytes =
        static_cast<std::uint64_t>(capture.at("max_file_bytes").integer());
    observer.observability.capture.max_packets =
        static_cast<std::uint64_t>(capture.at("max_packets").integer());
    demo::RuntimeTraceSink trace(settings.id(), observer);
    TransportFactory factory;
    TransportPtr input, output;
    const PortBinding* outbound{};
    for (const auto& [port, peers] : settings.bindings) {
      (void)port;
      for (const auto& peer : peers) {
        if (peer.role == ConnectionMode::listen)
          input = factory.create(peer.edge, peer.role, &trace, demo::stopping);
        else
          outbound = &peer;
      }
    }
    if (!await_node_release(settings, arguments, demo::stopping)) return 130;
    if (outbound) output = factory.create(outbound->edge, outbound->role, &trace, demo::stopping);
    // Closing an established transport wakes blocking sends without racing its lifetime.
    std::jthread interruption([&](std::stop_token token) {
      while (!token.stop_requested() && !demo::stopping())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (demo::stopping()) {
        if (input) input->close();
        if (output) output->close();
      }
    });
    const auto& parameters = node.at("parameters");
    const auto maximum = static_cast<std::uint64_t>(parameters.at("max_messages").integer());
    const auto interval = std::chrono::milliseconds(
        parameters.contains("interval_ms") ? parameters.at("interval_ms").integer() : 100);
    const auto factor = parameters.contains("factor") ? parameters.at("factor").integer() : 1;
    std::uint64_t count{};
    for (; count < maximum && !demo::stopping();) {
      trace.heartbeat();
      if (!input && trace.paused()) {
        demo::interruptible_pause(std::chrono::milliseconds(50));
        continue;
      }
      Envelope envelope;
      if (input) {
        auto received = input->receive_result(std::chrono::milliseconds(100));
        if (received.status == ReceiveStatus::timeout) continue;
        if (received.status != ReceiveStatus::message) break;
        envelope = std::move(*received.envelope);
        const auto& expected = type == "sample.transform" ? settings.port("samples")
                               : type == "sample.sink"    ? settings.port("transformed")
                                                          : settings.port("in");
        if (envelope.type != expected.schema)
          throw std::runtime_error("E_MESSAGE_SCHEMA: unexpected envelope type");
        if (output) {
          std::int64_t value{};
          const auto [end, error] = std::from_chars(
              envelope.payload.data(), envelope.payload.data() + envelope.payload.size(), value);
          if (error != std::errc{} || end != envelope.payload.data() + envelope.payload.size() ||
              value > std::numeric_limits<std::int64_t>::max() / factor ||
              value < std::numeric_limits<std::int64_t>::min() / factor)
            throw std::runtime_error("E_MESSAGE_SCHEMA: invalid or overflowing sample");
          envelope.payload = std::to_string(value * factor);
          envelope.type = outbound->schema;
          envelope.attributes["operation"] = "multiply-by-" + std::to_string(factor);
        }
      } else {
        envelope = Envelope::make(count + 1, outbound->schema,
                                  type == "udp.publisher" ? "message-" + std::to_string(count + 1)
                                                          : std::to_string(count + 1));
        envelope.attributes["source"] = settings.id();
      }
      trace.on_processing(settings.id(), envelope, std::chrono::nanoseconds{}, true);
      if (output) output->send(envelope);
      std::cout << "node=" << settings.id() << " seq=" << envelope.sequence
                << " value=" << envelope.payload << " trace=" << envelope.trace_id << std::endl;
      ++count;
      if (!input && count < maximum) demo::interruptible_pause(interval);
    }
    if (!demo::stopping() && count != maximum)
      throw std::runtime_error("E_STREAM_INCOMPLETE: peer closed before max_messages");
    return demo::stopping() ? 130 : 0;
  } catch (const std::exception& error) {
    std::cerr << type << ": " << error.what() << '\n';
    return demo::stopping() ? 130 : 1;
  }
}
}  // namespace graphx
