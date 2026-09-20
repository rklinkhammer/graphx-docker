// Exercise the real host adapter with authenticated TLS over bounded BIO pairs.
// Only the socket byte pump is test-owned; VITA encoding and leases use the library.
#include "graphx/vita/runtime.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <source_location>
#include <thread>
using namespace graphx::vita;
namespace rt = vr::runtime;
namespace tx = rt::transport;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) throw std::runtime_error("transport assertion line " + std::to_string(where.line()));
}
struct TlsPair {
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> server{SSL_CTX_new(TLS_server_method()),
                                                           SSL_CTX_free};
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> client{SSL_CTX_new(TLS_client_method()),
                                                           SSL_CTX_free};
  std::unique_ptr<SSL, decltype(&SSL_free)> radio{nullptr, SSL_free}, peer{nullptr, SSL_free};
  explicit TlsPair(const std::string& root) {
    for (auto [ctx, name] :
         {std::pair{server.get(), "radio"}, std::pair{client.get(), "controller"}}) {
      check(ctx);
      check(SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) == 1);
      check(SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION) == 1);
      SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
      check(SSL_CTX_load_verify_locations(ctx, (root + "/ca.pem").c_str(), nullptr) == 1);
      check(SSL_CTX_use_certificate_chain_file(ctx, (root + "/" + name + "/cert.pem").c_str()) ==
            1);
      check(SSL_CTX_use_PrivateKey_file(ctx, (root + "/" + name + "/key.pem").c_str(),
                                        SSL_FILETYPE_PEM) == 1);
    }
    radio.reset(SSL_new(server.get()));
    peer.reset(SSL_new(client.get()));
    BIO *a{}, *b{};
    check(BIO_new_bio_pair(&a, 256, &b, 256) == 1);
    SSL_set_bio(radio.get(), a, a);
    SSL_set_bio(peer.get(), b, b);
    SSL_set_accept_state(radio.get());
    SSL_set_connect_state(peer.get());
    for (int i = 0;
         i < 10000 && (!SSL_is_init_finished(radio.get()) || !SSL_is_init_finished(peer.get()));
         ++i) {
      for (SSL* ssl : {radio.get(), peer.get()}) {
        if (SSL_is_init_finished(ssl)) continue;
        int n = SSL_do_handshake(ssl);
        int error = SSL_get_error(ssl, n);
        check(n == 1 || error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
      }
    }
    check(SSL_is_init_finished(radio.get()) && SSL_is_init_finished(peer.get()));
    check(SSL_set_max_send_fragment(radio.get(), 512) == 1);
    SSL_set_mode(radio.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
  }
};
struct Backing {
  std::array<std::byte, 2048 * 32> bytes{};
  std::size_t returned{};
};
int main(int argc, char** argv) try {
  if (argc != 2) return 2;
  auto backing = std::make_shared<Backing>();
  vr::memory::BufferSpec spec{
      backing,
      backing->bytes.data(),
      2048,
      32,
      1,
      vr::memory::MemoryDomain::cpu,
      0,
      [](void* p, std::size_t) noexcept { ++static_cast<Backing*>(p)->returned; },
      backing.get()};
  auto made = vr::memory::ExternalPool::create({&spec, 1});
  check(bool(made));
  auto pool = std::move(*made);
  rt::AdmissionRequest limits;
  limits.need(rt::Resource::completion, 32);
  rt::AdmissionPool admission(limits);
  rt::RouteRegistry<128> routes;
  rt::CounterRegistry<128> counters;
  rt::CounterKey command{1, 1, vr::codec::PacketType::command};
  rt::CounterKey data{1, 1, vr::codec::PacketType::signal};
  rt::CounterKey context{1, 1, vr::codec::PacketType::context};
  for (auto key : {command, data, context}) check(bool(counters.add(key)));
  counters.freeze();
  routes.freeze();
  rt::CompletionArena<32> completions;
  int udp = socket(AF_INET, SOCK_DGRAM, 0), receiver = socket(AF_INET, SOCK_DGRAM, 0);
  check(udp >= 0 && receiver >= 0);
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  check(bind(receiver, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) == 0);
  socklen_t length = sizeof(destination);
  check(getsockname(receiver, reinterpret_cast<sockaddr*>(&destination), &length) == 0);
  auto transport = std::make_shared<HostTransport>(udp, destination);
  auto factory = transport->factory();
  auto made_binding =
      factory.create(factory.context, {admission, routes, counters, pool, pool, pool});
  check(bool(made_binding));
  auto binding = std::move(*made_binding);
  std::uint64_t next_id = 1;
  std::size_t accepted{}, completed{}, failed{}, succeeded{};
  std::array<std::byte, 1024> expected_wire{};
  auto submit = [&](rt::CounterKey key, bool cancel = false) {
    auto block = pool.acquire({1024});
    check(bool(block));
    auto bytes = block->writable_bytes();
    check(bool(bytes));
    vr::codec::Envelope envelope;
    envelope.type = key.type;
    envelope.stream_id = 1;
    envelope.packet_count = *counters.next(key);
    envelope.cancel = cancel;
    if (key.type == command.type)
      envelope.command = vr::codec::Command{cancel ? 0x09080000U : 0x00040000U,
                                            static_cast<std::uint32_t>(next_id)};
    std::array<std::byte, 1008> payload{};
    auto n = vr::codec::encode_envelope(
        envelope, key.type == command.type ? vr::Bytes{payload} : vr::Bytes{payload}.first(4),
        std::nullopt, *bytes);
    check(bool(n) && bool(block->set_size(*n)));
    if (key.type == command.type) std::copy_n(bytes->begin(), *n, expected_wire.begin());
    vr::memory::TxStorage storage;
    check(bool(storage.append(std::move(*block), 0, *n)));
    auto ticket = completions.reserve(next_id++);
    check(bool(ticket));
    auto credit = admission.acquire(rt::AdmissionRequest{}.need(rt::Resource::completion));
    check(bool(credit));
    auto result = binding.try_send(
        {std::move(storage), std::move(*ticket), {1, 1}, key, {}, std::move(*credit)});
    if (result) ++accepted;
    return result;
  };
  auto scan = [&] {
    completions.scan([&](rt::CompletionRecord record) noexcept {
      if (record.result.status == rt::CompletionStatus::abandoned) return;
      ++completed;
      if (record.result.status == rt::CompletionStatus::succeeded)
        ++succeeded;
      else
        ++failed;
    });
  };
  {
    TlsPair tls(argv[1]);
    transport->connect(tls.radio.get());
    auto one = submit(command), two = submit(command), three = submit(command);
    check(one && two && three);  // 3072 ordinary bytes; cancellation reserve remains available.
    {
      auto rejected = submit(command);
      check(!rejected && rejected.error().error.code == vr::ErrorCode::capacity_exhausted);
    }
    auto cancel = submit(command, true);
    check(bool(cancel));
    for (int i = 0; i < 100; ++i) {
      check(bool(binding.progress_next()));
      scan();
    }
    check(completed == 0 && binding.outstanding(*one));  // peer deliberately never reads
    // Even permanently queued datagrams must not starve the control write deadline.
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(2200);
    while (std::chrono::steady_clock::now() < end) {
      for (auto key : {context, data}) {
        auto token = submit(key);
        check(bool(token));
        check(bool(binding.progress_next()));
        std::array<std::byte, 64> wire{};
        check(recv(receiver, wire.data(), wire.size(), 0) > 0);
      }
      scan();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!transport->healthy());
    check(failed == 4 && succeeded > 100);
    transport->disconnect();
    transport->disconnect();
    scan();
    check(completed == accepted);
    check(!binding.outstanding(*one) && bool(binding.prove_quiescent(*one)));
  }
  // A fresh authenticated socket recovers the same host binding and counters.
  {
    TlsPair tls(argv[1]);
    transport->connect(tls.radio.get());
    check(transport->healthy());
    auto token = submit(command);
    check(bool(token));
    std::size_t received = 0;
    std::array<std::byte, 1024> received_wire{};
    bool partial = false;
    for (int i = 0; i < 10000 && (binding.outstanding(*token) || received < 1024); ++i) {
      check(bool(binding.progress_next()));
      std::array<std::byte, 83> bytes{};
      int n = SSL_read(tls.peer.get(), bytes.data(), bytes.size());
      if (n > 0) {
        check(received + static_cast<std::size_t>(n) <= received_wire.size());
        std::copy_n(bytes.begin(), n, received_wire.begin() + received);
        received += n;
      } else {
        int error = SSL_get_error(tls.peer.get(), n);
        check(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
      }
      if (received > 0 && binding.outstanding(*token)) partial = true;
    }
    check(received == 1024 && partial && !binding.outstanding(*token));
    check(received_wire == expected_wire);
    scan();
    check(completed == accepted);
    auto pending = submit(command);
    check(bool(pending));
    check(bool(binding.progress_next()));
    transport->disconnect();
    scan();
    check(completed == accepted && failed == 5);
    transport->disconnect();
    scan();
    check(completed == accepted);
  }
  {
    TlsPair tls(argv[1]);
    transport->connect(tls.radio.get());
    auto pending = submit(command);
    check(bool(pending));
    check(bool(binding.progress_next()));
    check(binding.outstanding(*pending));
    tls.peer.reset();  // Abrupt byte-channel loss during a pending TLS record.
    for (int i = 0; i < 4; ++i) check(bool(binding.progress_next()));
    check(!transport->healthy() && !binding.outstanding(*pending));
    scan();
    check(failed == 6 && completed == accepted);
    transport->disconnect();
  }
  check(admission.used(rt::Resource::completion) == 0);
  binding.detach(binding.context);
  scan();
  check(completed == accepted);
  check(backing->returned ==
        next_id - 1);  // including synchronous rejection, every lease returned once
  close(receiver);
  close(udp);
  std::cout << "bounded TLS saturation, partial writes, datagrams, deadline, reconnect and "
               "exactly-once completion passed\n";
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
