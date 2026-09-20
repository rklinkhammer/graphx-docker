// Cross-process client for the production ControllerSession/mTLS transport.
#include "graphx/vita/runtime.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <thread>
using namespace graphx::vita;
using namespace std::chrono;
void require(bool value) {
  if (!value) throw std::runtime_error("controller integration assertion");
}
int main(int argc, char** argv) try {
  if (argc != 4) return 2;
  const auto port = std::stoi(argv[1]);
  const auto id = static_cast<std::uint32_t>(std::stoul(argv[2]));
  const std::string credentials = argv[3];
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_client_method()),
                                                            SSL_CTX_free);
  require(bool(context));
  require(SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) == 1);
  SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
  require(SSL_CTX_load_verify_locations(context.get(), (credentials + "/ca.pem").c_str(),
                                        nullptr) == 1);
  require(SSL_CTX_use_certificate_chain_file(context.get(),
                                             (credentials + "/controller/cert.pem").c_str()) == 1);
  require(SSL_CTX_use_PrivateKey_file(context.get(), (credentials + "/controller/key.pem").c_str(),
                                      SSL_FILETYPE_PEM) == 1);
  ControllerSession session(id);
  int fd = -1;
  std::unique_ptr<SSL, decltype(&SSL_free)> ssl(nullptr, SSL_free);
  const auto connect_peer = [&] {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    require(fd >= 0);
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    require(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    ssl.reset(SSL_new(context.get()));
    require(bool(ssl));
    require(SSL_set_fd(ssl.get(), fd) == 1 && SSL_set1_host(ssl.get(), "radio") == 1);
    require(SSL_connect(ssl.get()) == 1);
    require(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    session.connect(ssl.get());
  };
  connect_peer();
  auto& controller = session.commands();
  const auto wait = [&](vr::Result<vr::TransactionHandle> handle, vr::WaitEvidence evidence) {
    require(bool(handle));
    auto deadline = steady_clock::now() + seconds(2);
    while (steady_clock::now() < deadline) {
      require(session.progress());
      auto result = controller.wait(*handle, 0, evidence);
      require(bool(result));
      if (result->status == vr::WaitStatus::evidence_received) return *handle;
      std::this_thread::sleep_for(microseconds(100));
    }
    throw std::runtime_error("controller evidence timeout");
  };
  const auto capabilities = [&] {
    auto handle = wait(controller.query_capabilities(), vr::WaitEvidence::state);
    auto limits = controller.capabilities(handle);
    require(bool(limits) && bool(*limits));
    require((**limits).range<vr::SampleRate>()->maximum == *vr::Hertz::from_integer(2000000));
    require(bool(controller.release(handle)));
  };
  capabilities();
  vr::GraphxRadioSettings settings;
  settings.sample_rate = *vr::Hertz::from_integer(1000003);
  settings.bandwidth = *vr::Hertz::from_integer(800000);
  auto configured = wait(controller.configure(settings), vr::WaitEvidence::state);
  auto applied = controller.state(configured);
  require(bool(applied) && bool(*applied));
  require((**applied).value<vr::SampleRate>() == settings.sample_rate);
  auto when = *vr::runtime::timing::add(session.now(), {0, 250000000000});
  auto start = wait(controller.start(when), vr::WaitEvidence::validation);
  auto early = controller.wait(start, 0, vr::WaitEvidence::execution);
  require(bool(early) && early->status == vr::WaitStatus::wait_budget_expired);
  require(bool(controller.cancel(start, vr::QuerySelection{vr::QueryField::streaming})));
  wait(start, vr::WaitEvidence::cancellation_execution);
  when = *vr::runtime::timing::add(session.now(), {0, 250000000000});
  auto running = wait(controller.start(when), vr::WaitEvidence::execution);
  require(controller.observation(running)->confirms_execution);
  capabilities();
  session.disconnect();
  ssl.reset();
  close(fd);
  std::this_thread::sleep_for(milliseconds(30));
  connect_peer();  // Same Controller/runtime, new authenticated socket.
  auto status = wait(controller.status(), vr::WaitEvidence::state);
  auto state = controller.state(status);
  require(bool(state) && bool(*state));
  require((**state).value<vr::DiscreteIO32>() == 3);
  auto stopped = wait(controller.stop(), vr::WaitEvidence::execution);
  require(controller.observation(stopped)->confirms_execution);
  capabilities();
  session.disconnect();
  ssl.reset();
  close(fd);
  std::cout
      << "controller APIs: capabilities/configure/arm/cancel/start/reconnect/status/stop passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
