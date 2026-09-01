#include "graphx/tcp_transport.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace {

graphx::Envelope message() { return graphx::Envelope::make(1, "security.smoke", "*"); }

graphx::TcpOptions tls_options(const char* certificate, const char* key, const char* ca) {
  graphx::TcpOptions options;
  options.connect_timeout = std::chrono::seconds(2);
  options.send_timeout = std::chrono::seconds(2);
  options.reconnect = true;
  options.tls.enabled = true;
  options.tls.ca_file = ca;
  options.tls.certificate_file = certificate;
  options.tls.private_key_file = key;
  options.tls.server_name = "localhost";
  options.tls.require_client_certificate = true;
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) return 64;
  try {
    const auto port = static_cast<std::uint16_t>(std::stoi(argv[4]));
    auto options = tls_options(argv[1], argv[2], argv[3]);
    auto listener =
        graphx::TcpTransport::listen({"127.0.0.1", port}, "tls-smoke", nullptr, options);
    auto received =
        std::async(std::launch::async, [&] { return listener.receive(std::chrono::seconds(3)); });
    auto client = graphx::TcpTransport::connect({"127.0.0.1", port}, "tls-smoke", nullptr, options);
    client.send(message());
    const auto value = received.get();
    if (!value || value->sequence != 1 || value->payload != message().payload)
      throw std::runtime_error("TLS round trip returned the wrong envelope");
    client.close();

    auto reconnected =
        std::async(std::launch::async, [&] { return listener.receive(std::chrono::seconds(3)); });
    auto second = graphx::TcpTransport::connect({"127.0.0.1", port}, "tls-smoke", nullptr, options);
    second.send(message());
    if (!reconnected.get()) throw std::runtime_error("TLS reconnect did not deliver a frame");
    second.close();
    listener.close();

    const auto mismatch_port = static_cast<std::uint16_t>(port + 1);
    auto mismatch_listener = graphx::TcpTransport::listen({"127.0.0.1", mismatch_port},
                                                          "tls-mismatch", nullptr, options);
    auto accepting = std::async(std::launch::async, [&] {
      try {
        (void)mismatch_listener.receive(std::chrono::seconds(3));
      } catch (...) {
      }
    });
    auto mismatch_options = options;
    mismatch_options.tls.server_name = "not-localhost.invalid";
    bool rejected{};
    try {
      auto invalid = graphx::TcpTransport::connect({"127.0.0.1", mismatch_port}, "tls-mismatch",
                                                   nullptr, mismatch_options);
    } catch (...) {
      rejected = true;
    }
    mismatch_listener.close();
    accepting.get();
    if (!rejected) throw std::runtime_error("TLS peer-name mismatch was accepted");

    const auto untrusted_port = static_cast<std::uint16_t>(port + 2);
    auto untrusted_listener = graphx::TcpTransport::listen({"127.0.0.1", untrusted_port},
                                                           "tls-untrusted", nullptr, options);
    auto untrusted_accept = std::async(std::launch::async, [&] {
      try {
        (void)untrusted_listener.receive(std::chrono::seconds(3));
        return false;
      } catch (...) {
        return true;
      }
    });
    auto untrusted_options = options;
    untrusted_options.tls.ca_file = argv[5];
    rejected = false;
    try {
      auto invalid = graphx::TcpTransport::connect({"127.0.0.1", untrusted_port}, "tls-untrusted",
                                                   nullptr, untrusted_options);
    } catch (...) {
      rejected = true;
    }
    untrusted_listener.close();
    (void)untrusted_accept.get();
    if (!rejected) throw std::runtime_error("TLS untrusted certificate chain was accepted");

    const auto no_client_port = static_cast<std::uint16_t>(port + 3);
    auto no_client_listener = graphx::TcpTransport::listen({"127.0.0.1", no_client_port},
                                                           "tls-no-client", nullptr, options);
    auto no_client_accept = std::async(std::launch::async, [&] {
      try {
        (void)no_client_listener.receive(std::chrono::seconds(3));
        return false;
      } catch (...) {
        return true;
      }
    });
    auto no_client_options = options;
    no_client_options.tls.certificate_file.clear();
    no_client_options.tls.private_key_file.clear();
    try {
      auto invalid = graphx::TcpTransport::connect({"127.0.0.1", no_client_port}, "tls-no-client",
                                                   nullptr, no_client_options);
      invalid.send(message());
    } catch (...) { /* The peer may deliver the certificate alert during connect or send. */
    }
    const bool missing_client_rejected = no_client_accept.get();
    no_client_listener.close();
    if (!missing_client_rejected)
      throw std::runtime_error("TLS listener accepted a missing client certificate");

    const auto cancellation_port = static_cast<std::uint16_t>(port + 4);
    auto cancellation_listener = graphx::TcpTransport::listen({"127.0.0.1", cancellation_port},
                                                              "tls-cancel", nullptr, options);
    auto blocked =
        std::async(std::launch::async, [&] { return cancellation_listener.receive_result(); });
    auto plaintext = graphx::TcpTransport::connect({"127.0.0.1", cancellation_port});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancellation_listener.close();
    if (blocked.wait_for(std::chrono::seconds(1)) != std::future_status::ready ||
        blocked.get().status != graphx::ReceiveStatus::cancelled)
      throw std::runtime_error("TLS handshake did not honor transport cancellation");
    plaintext.close();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
