#include <system_error>
#include "graphx/vita/virtual_device.hpp"
#include "graphx/node_settings.hpp"
#include "../application_observer.hpp"
#include "graphx/vita/runtime.hpp"
#include <SoapySDR/Constants.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <numbers>
#include <vector>
#include <thread>
namespace graphx::vita {
namespace {
using namespace std::chrono;
using Clock = steady_clock;
volatile std::sig_atomic_t stopping{};
void stop_signal(int) { stopping = 1; }
struct Fd {
  int value;
  explicit Fd(int value_) : value(value_) {
    if (value < 0) throw std::runtime_error("socket failed");
  }
  ~Fd() { close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
sockaddr_in address(const std::string& ip, std::int64_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  if (port < 1 || port > 65535 || inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1)
    throw std::runtime_error("invalid resolved endpoint");
  return a;
}
struct Client {
  Fd fd;
  std::unique_ptr<SSL, decltype(&SSL_free)> ssl;
  bool ready{}, bound{};
  Clock::time_point deadline{Clock::now() + seconds(2)};
  Client(int socket, SSL_CTX* ctx) : fd(socket), ssl(SSL_new(ctx), SSL_free) {
    if (!ssl || SSL_set_fd(ssl.get(), fd.value) != 1) throw std::runtime_error("TLS allocation");
    fcntl(fd.value, F_SETFL, O_NONBLOCK);
    SSL_set_accept_state(ssl.get());
  }
};
}  // namespace
int run_radio(int argc, char** argv) {
  try {
    auto args = node_arguments(argc, argv);
    auto node = load_node_settings(args.config, args.node, "vita.radio");
    auto tcp = std::get<TcpTransportConfig>(
        std::get<ExternalTransportConfig>(node.port("control").edge.transport).protocol);
    auto udp = std::get<UdpTransportConfig>(
        std::get<ExternalTransportConfig>(node.port("samples").edge.transport).protocol);
    if (!tcp.tls.enabled || udp.max_datagram_bytes < 4128)
      throw std::runtime_error("radio needs mTLS and 4128-byte datagrams");
    const auto generation_text = config_internal::read_document(tcp.tls.generation_file, 8192);
    const auto generation = config_internal::parse_document(generation_text);
    if (generation.at("version") != ConfigValue(1) || generation.at("generation").integer() < 1)
      throw std::runtime_error("invalid TLS credential generation");
    for (const auto& path : {tcp.tls.ca_file, tcp.tls.certificate_file, tcp.tls.private_key_file}) {
      const auto name = std::filesystem::path(path).filename().string();
      if (config_internal::sha256(config_internal::read_document(path, 65536)) !=
          generation.at("members").at(name).text())
        throw std::runtime_error("incomplete TLS credential generation");
    }
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_server_method()),
                                                          SSL_CTX_free);
    if (!ctx || SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION) != 1 ||
        SSL_CTX_load_verify_locations(ctx.get(), tcp.tls.ca_file.c_str(), nullptr) != 1 ||
        SSL_CTX_use_certificate_chain_file(ctx.get(), tcp.tls.certificate_file.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx.get(), tcp.tls.private_key_file.c_str(),
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx.get()) != 1)
      throw std::runtime_error("TLS credentials");
    if (generation_text != config_internal::read_document(tcp.tls.generation_file, 8192))
      throw std::runtime_error("TLS credential generation changed during loading");
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    Fd listener(socket(AF_INET, SOCK_STREAM, 0)), sender(socket(AF_INET, SOCK_DGRAM, 0));
    auto local = address(tcp.bind, tcp.port), destination = address(udp.destination, udp.port);
    auto source = address(udp.bind, 1);
    source.sin_port = 0;
    if (bind(listener.value, reinterpret_cast<sockaddr*>(&local), sizeof(local)) ||
        listen(listener.value, 4) ||
        bind(sender.value, reinterpret_cast<sockaddr*>(&source), sizeof(source)))
      throw std::system_error(errno, std::generic_category(), "radio bind");
    fcntl(listener.value, F_SETFL, O_NONBLOCK);
    fcntl(sender.value, F_SETFL, O_NONBLOCK);
    auto& parameters = node.resolved.at("parameters");
    HostClock clock;
    auto device = std::make_shared<DeviceBinding>(
        parameters.at("signal_frequency_hz").integer(),
        parameters.at("amplitude_ppm").integer() / 1000000.0,
        parameters.at("initial_phase_mdeg").integer() * std::numbers::pi / 180000.0);
    device->set_clock(&clock, [](void* p) noexcept { return static_cast<HostClock*>(p)->now(); });
    auto transport = std::make_shared<HostTransport>(sender.value, destination);
    auto config = runtime_config();
    config.transport = transport->factory();
    auto made = Runtime::create(config, runtime_pools());
    if (!made)
      throw std::runtime_error("VITA runtime setup " +
                               std::to_string(static_cast<int>(made.error().code)));
    auto runtime = std::move(*made);
    vr::StreamConfig stream;
    stream.sid = static_cast<std::uint32_t>(parameters.at("radio_index").integer());
    stream.controller_id = 1;
    stream.controllee_id = stream.sid;
    stream.profile = vr::profiles::iq::Profile::graphx_radio;
    stream.role = vr::EndpointRole::controllee_only;
    stream.trailer = true;
    stream.maximum_samples_per_packet = 1024;
    stream.burst_pairs = static_cast<std::size_t>(parameters.at("burst_samples").integer());
    stream.ip_mtu = 9000;
    stream.bandwidth = 800000;
    stream.graphx_capabilities = device->capabilities();
    stream.device = device->binding();
    stream.source = device->source();
    auto radio = runtime->add_controllee(stream);
    if (!radio)
      throw std::runtime_error("VITA radio setup " +
                               std::to_string(static_cast<int>(radio.error().code)));
    stopping = 0;
    std::signal(SIGINT, stop_signal);
    std::signal(SIGTERM, stop_signal);
    std::signal(SIGPIPE, SIG_IGN);
    if (!await_node_release(node, args, [] { return stopping != 0; })) return 0;
    std::unique_ptr<UdpJsonTraceSink> trace;
    if (!node.resolved.at("telemetry").at("credential").is_null()) {
      auto secret = demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET");
      if (secret.empty()) throw std::runtime_error("missing staged telemetry credential");
      trace = std::make_unique<UdpJsonTraceSink>(
          node.id(), node.resolved.at("telemetry").at("host").text(),
          node.resolved.at("telemetry").at("port").integer(), secret,
          [] { return demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET"); });
    }
    std::unique_ptr<Client> client;
    auto log_at = Clock::now() + seconds(1);
    while (!stopping) {
      clock.progress(*runtime);
      sockaddr_in peer{};
      socklen_t size = sizeof(peer);
      int socket = accept(listener.value, reinterpret_cast<sockaddr*>(&peer), &size);
      if (socket >= 0) {
        auto expected = address(node.port("control").source_address, 1);
        if (client || peer.sin_addr.s_addr != expected.sin_addr.s_addr)
          close(socket);
        else
          client = std::make_unique<Client>(socket, ctx.get());
      }
      if (client) {
        auto& c = *client;
        bool failed = !c.ready && Clock::now() > c.deadline;
        if (!failed && !c.ready) {
          int result = SSL_accept(c.ssl.get());
          if (result == 1) {
            auto* cert = SSL_get0_peer_certificate(c.ssl.get());
            c.ready = cert && X509_check_host(cert, "controller", 10,
                                              X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, nullptr) == 1;
            failed = !c.ready;
          } else {
            int error = SSL_get_error(c.ssl.get(), result);
            failed = error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE;
          }
        }
        if (!failed && c.ready) {
          if (!c.bound) {
            transport->connect(c.ssl.get());
            c.bound = true;
          }
          failed = !transport->receive() || !transport->healthy();
        }
        if (failed) {
          transport->disconnect();
          client.reset();
        }
      }
      if (Clock::now() >= log_at) {
        if (trace) {
          trace->on_heartbeat(node.id(), -1);
          trace->on_edge_totals(node.port("samples").edge.edge.id, true, transport->udp_sent,
                                transport->udp_sent_bytes);
        }
        std::cout << "radio=" << stream.sid << " packets=" << radio->metrics().packets
                  << " dropped_samples=" << radio->metrics().skipped_samples
                  << " udp_errors=" << transport->udp_errors
                  << " clipped=" << device->device.clipped() << std::endl;
        log_at = Clock::now() + seconds(1);
      }
      poll(nullptr, 0, 0);
      // Sub-millisecond pacing without a busy spin; control stays serviced.
      std::this_thread::sleep_for(microseconds(100));
    }
    transport->disconnect();
    runtime->shutdown(vr::StopMode::immediate);
    clock.progress(*runtime);
    return 0;
  } catch (const std::system_error& e) {
    std::cerr << "graphx-vita-radio: " << e.what() << '\n';
    return e.code() == std::errc::address_in_use ? 75 : 78;
  } catch (const std::exception& e) {
    std::cerr << "graphx-vita-radio: " << e.what() << '\n';
    return 78;
  }
}
}  // namespace graphx::vita
