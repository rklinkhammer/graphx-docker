#include "graphx/vita/processing.hpp"
#include "graphx/node_settings.hpp"
#include "../application_observer.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <thread>
namespace graphx::vita {
namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
struct Socket {
  int fd{-1};
  Socket() = default;
  explicit Socket(int type) : fd(::socket(AF_INET, type, 0)) {
    if (fd < 0) throw std::runtime_error("socket");
    fcntl(fd, F_SETFL, O_NONBLOCK);
  }
  ~Socket() {
    if (fd >= 0) close(fd);
  }
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
};
sockaddr_in address(const std::string& host, std::int64_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (port < 0 || port > 65535 || inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1)
    throw std::runtime_error("endpoint");
  return a;
}
void bind_socket(int fd, const std::string& host, std::int64_t port) {
  auto a = address(host, port);
  if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a))) throw std::runtime_error("bind");
}
void udp_options(int fd, const UdpTransportConfig& cfg) {
  if (cfg.mode != UdpMode::unicast || cfg.framing != "none")
    throw std::runtime_error("VITA application requires raw unicast UDP");
  int receive = cfg.receive_buffer_bytes, send = cfg.send_buffer_bytes, reuse = cfg.reuse_address;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive, sizeof(receive)) ||
      setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send, sizeof(send)) ||
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)))
    throw std::runtime_error("UDP socket bounds");
}
const UdpTransportConfig& udp(const PortBinding& p) {
  return std::get<UdpTransportConfig>(std::get<ExternalTransportConfig>(p.edge.transport).protocol);
}
const TcpTransportConfig& tcp(const PortBinding& p) {
  return std::get<TcpTransportConfig>(std::get<ExternalTransportConfig>(p.edge.transport).protocol);
}
std::int64_t integer(const NodeSettings& s, const std::string& name) {
  return s.resolved.at("parameters").at(name).integer();
}
struct Log {
  std::deque<std::string> queue;
  std::size_t offset{};
  std::uint64_t drops{};
  Log() { fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK); }
  void push(std::string text) {
    if (queue.size() == 64) {
      ++drops;
      return;
    }
    queue.push_back(std::move(text) + '\n');
  }
  void progress() {
    for (int i = 0; i < 16 && !queue.empty(); ++i) {
      auto& s = queue.front();
      auto n = write(STDOUT_FILENO, s.data() + offset, s.size() - offset);
      if (n <= 0) return;
      offset += n;
      if (offset == s.size()) {
        queue.pop_front();
        offset = 0;
      }
    }
  }
};
struct Connection {
  ControllerSession session;
  TcpTransportConfig config;
  std::unique_ptr<Socket> socket;
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx{nullptr, SSL_CTX_free};
  std::unique_ptr<SSL, decltype(&SSL_free)> ssl{nullptr, SSL_free};
  Clock::time_point deadline{}, retry{}, query_after{};
  bool connected{}, initial_sent{}, configured{}, start_sent{};
  std::optional<vr::TransactionHandle> pending, start_handle;
  bool executed{}, reported{}, ever_connected{};
  std::uint32_t attempts{};
  vr::GraphxRadioSettings settings;
  Connection(std::uint32_t sid, const PortBinding& port, SampleAssembler& assembler)
      : session(sid, {&assembler,
                      [](void* p, const auto& rx) noexcept {
                        static_cast<SampleAssembler*>(p)->receive(rx);
                      },
                      [](void* p, auto) noexcept { ++static_cast<SampleAssembler*>(p)->invalid; }}),
        config(tcp(port)) {
    if (!config.tls.enabled) throw std::runtime_error("processor requires mTLS");
    const auto text = config_internal::read_document(config.tls.generation_file, 8192);
    auto generation = config_internal::parse_document(text);
    if (generation.at("version").integer() != 1 || generation.at("generation").integer() < 1)
      throw std::runtime_error("credential generation");
    for (const auto& path :
         {config.tls.ca_file, config.tls.certificate_file, config.tls.private_key_file})
      if (config_internal::sha256(config_internal::read_document(path, 65536)) !=
          generation.at("members").at(std::filesystem::path(path).filename().string()).text())
        throw std::runtime_error("credential digest");
    ctx.reset(SSL_CTX_new(TLS_client_method()));
    if (!ctx || SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION) != 1 ||
        SSL_CTX_load_verify_locations(ctx.get(), config.tls.ca_file.c_str(), nullptr) != 1 ||
        SSL_CTX_use_certificate_chain_file(ctx.get(), config.tls.certificate_file.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx.get(), config.tls.private_key_file.c_str(),
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx.get()) != 1)
      throw std::runtime_error("TLS setup");
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    if (text != config_internal::read_document(config.tls.generation_file, 8192))
      throw std::runtime_error("credential rotation during setup");
  }
  ~Connection() { session.disconnect(); }
  void disconnect() {
    session.disconnect();
    connected = false;
    ssl.reset();
    socket.reset();
    auto shift = std::min(attempts, 16u);
    auto delay = std::min<std::uint64_t>(static_cast<std::uint64_t>(config.retry.initial_backoff_ms)
                                             << shift,
                                         config.retry.max_backoff_ms);
    retry = Clock::now() + std::chrono::milliseconds(delay);
  }
  void progress(bool initialize) {
    auto now = Clock::now();
    if (!connected) session.progress();
    if (!socket) {
      if (now < retry || attempts >= config.retry.max_attempts ||
          (ever_connected && !config.reconnect))
        return;
      ++attempts;
      socket = std::make_unique<Socket>(SOCK_STREAM);
      bind_socket(socket->fd, config.source_address, 0);
      auto peer = address(config.host, config.port);
      if (::connect(socket->fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) &&
          errno != EINPROGRESS) {
        disconnect();
        return;
      }
      ssl.reset(SSL_new(ctx.get()));
      if (!ssl || SSL_set_fd(ssl.get(), socket->fd) != 1 ||
          SSL_set1_host(ssl.get(), config.tls.server_name.c_str()) != 1)
        throw std::runtime_error("TLS connect");
      SSL_set_connect_state(ssl.get());
      deadline = now + std::chrono::milliseconds(std::min(config.connect_timeout_ms, 2000u));
    }
    if (!connected) {
      if (now >= deadline) {
        disconnect();
        return;
      }
      int n = SSL_connect(ssl.get());
      if (n != 1) {
        int error = SSL_get_error(ssl.get(), n);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) disconnect();
        return;
      }
      session.connect(ssl.get());
      connected = ever_connected = true;
      attempts = 0;
      if (pending) {
        session.commands().release(*pending);
        pending.reset();
      }
      if (initial_sent) {
        auto query = session.commands().status();
        if (query) pending = *query;
      }
    }
    if (!session.progress()) {
      disconnect();
      return;
    }
    if (pending) {
      auto observed = session.commands().wait(*pending, 0, vr::WaitEvidence::state);
      if (observed && observed->status == vr::WaitStatus::evidence_received) {
        auto state = session.commands().state(*pending);
        if (state && *state) {
          auto rate = (**state).value<vr::SampleRate>();
          auto center = (**state).value<vr::RFReferenceFrequency>();
          auto bandwidth = (**state).value<vr::Bandwidth>();
          auto gain = (**state).value<vr::Gain>();
          configured = rate == settings.sample_rate && center == settings.center_frequency &&
                       bandwidth == settings.bandwidth && gain == settings.gain;
        }
        session.commands().release(*pending);
        pending.reset();
      } else if (observed && observed->status == vr::WaitStatus::transaction_timeout) {
        session.commands().release(*pending);
        pending.reset();
      }
    }
    if (initialize && !initial_sent && !pending) {
      auto h = session.commands().configure(settings);
      if (h) {
        pending = *h;
        initial_sent = true;
      }
    }
    if (!pending && initial_sent && now >= query_after) {
      auto h = session.commands().status();
      if (h) pending = *h;
      query_after = now + std::chrono::seconds(1);
    }
    if (start_handle) {
      auto r = session.commands().wait(*start_handle, 0, vr::WaitEvidence::execution);
      if (r && r->status != vr::WaitStatus::wait_budget_expired) {
        executed = r->observation.confirms_execution;
        session.commands().release(*start_handle);
        start_handle.reset();
      }
    }  // bounded by reply/turn, no reconfiguration
  }
};
std::unique_ptr<UdpJsonTraceSink> observer(const NodeSettings& node) {
  if (node.resolved.at("telemetry").at("credential").is_null()) return {};
  auto secret = demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET");
  if (secret.empty()) throw std::runtime_error("telemetry credentials");
  return std::make_unique<UdpJsonTraceSink>(
      node.id(), node.resolved.at("telemetry").at("host").text(),
      node.resolved.at("telemetry").at("port").integer(), secret,
      [] { return demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET"); });
}
}  // namespace
int run_processor(int argc, char** argv) try {
  auto args = node_arguments(argc, argv);
  auto node = load_node_settings(args.config, args.node, "vita.processor");
  stopped = 0;
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  std::signal(SIGPIPE, SIG_IGN);
  Socket output(SOCK_DGRAM);
  auto out = udp(node.port("spectra"));
  udp_options(output.fd, out);
  bind_socket(output.fd, node.port("spectra").source_address, 0);
  auto destination = address(out.destination, out.port);
  std::uint64_t spectra{}, drops{}, invalid{};
  Log log;
  std::array<std::unique_ptr<Socket>, 4> inputs;
  std::array<std::unique_ptr<SampleAssembler>, 4> assemblers;
  std::array<std::unique_ptr<Connection>, 4> connections;
  std::array<in_addr_t, 4> sources{};
  for (int i = 0; i < 4; ++i) {
    auto suffix = std::to_string(i + 1);
    auto rate = integer(node, "rate" + suffix);
    auto numerator = integer(node, "bin_width_numerator_hz" + suffix),
         denominator = integer(node, "bin_width_denominator" + suffix);
    if (rate * denominator % numerator) throw std::runtime_error("unrepresentable bin width");
    ProcessingConfig c{static_cast<std::uint32_t>(rate),
                       static_cast<std::uint32_t>(rate * denominator / numerator),
                       static_cast<std::uint32_t>(integer(node, "overlap_percent")),
                       integer(node, "window") != 0};
    c.validate();
    if (spectrum_bytes(c.size) + 28 > static_cast<std::size_t>(integer(node, "path_mtu")) ||
        out.max_datagram_bytes < spectrum_bytes(c.size))
      throw std::runtime_error("FFT datagram budget");
    assemblers[i] = std::make_unique<SampleAssembler>(i + 1, c, [&](Spectrum spectrum) {
      auto wire = encode_spectrum(spectrum);
      ++spectra;
      auto sent = sendto(output.fd, wire.data(), wire.size(), 0,
                         reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
      if (sent != static_cast<ssize_t>(wire.size())) ++drops;
    });
    auto& port = node.port("data" + suffix);
    auto input = udp(port);
    inputs[i] = std::make_unique<Socket>(SOCK_DGRAM);
    udp_options(inputs[i]->fd, input);
    if (input.max_datagram_bytes < 4128) throw std::runtime_error("IQ datagram budget");
    bind_socket(inputs[i]->fd, input.bind, input.port);
    sources[i] = address(port.source_address, 0).sin_addr.s_addr;
    connections[i] =
        std::make_unique<Connection>(i + 1, node.port("control" + suffix), *assemblers[i]);
    auto& settings = connections[i]->settings;
    settings.sample_rate = *vr::Hertz::from_integer(rate);
    settings.center_frequency = *vr::Hertz::from_integer(integer(node, "center" + suffix));
    settings.bandwidth = *vr::Hertz::from_integer(integer(node, "bandwidth" + suffix));
    settings.gain = {static_cast<std::int16_t>(integer(node, "gain_db" + suffix) * 128), 0};
  }
  auto trace = observer(node);
  if (!await_node_release(node, args, [] { return stopped != 0; })) return 0;
  auto configure_until = Clock::now() + std::chrono::seconds(5), summary = Clock::now();
  bool scheduled = false;
  while (!stopped) {
    bool ready = true;
    for (auto& c : connections) {
      c->progress(!scheduled && Clock::now() < configure_until);
      ready &= c->configured;
    }
    if (!scheduled && (ready || Clock::now() >= configure_until)) {
      auto when = *vr::runtime::timing::add(connections[0]->session.now(), {0, 250000000000});
      for (std::size_t i = 0; i < connections.size(); ++i) {
        auto& c = connections[i];
        if (c->configured && c->connected) {
          auto start = c->session.commands().start(when);
          if (start) {
            assemblers[i]->start_epoch(when);
            c->start_sent = true;
            c->start_handle = *start;
          }
        }
      }
      scheduled = true;
    }
    for (int i = 0; i < 4; ++i) {
      auto& c = *connections[i];
      if (c.start_sent && !c.start_handle && !c.reported) {
        log.push("controller stream=" + std::to_string(i + 1) +
                 " executed=" + std::to_string(c.executed));
        c.reported = true;
      }
      for (int work = 0; work < 16; ++work) {
        std::array<std::byte, 4129> bytes{};
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        auto n = recvfrom(inputs[i]->fd, bytes.data(), bytes.size(), 0,
                          reinterpret_cast<sockaddr*>(&from), &length);
        if (n < 0) break;
        if (from.sin_addr.s_addr != sources[i] || n > 4128 ||
            !assemblers[i]->observe(std::span{bytes}.first(n)) ||
            !connections[i]->session.datagram(std::span{bytes}.first(n)))
          ++invalid;
      }
      assemblers[i]->progress();
    }
    if (Clock::now() >= summary) {
      std::uint64_t sample_invalid{}, duplicates{}, skipped{}, sequence_gaps{};
      for (const auto& a : assemblers) {
        sample_invalid += a->invalid;
        duplicates += a->duplicates;
        skipped += a->skipped;
        sequence_gaps += a->sequence_gaps;
      }
      std::ostringstream text;
      text << "processor spectra=" << spectra << " send_drops=" << drops << " invalid=" << invalid
           << " sample_invalid=" << sample_invalid << " duplicates=" << duplicates
           << " skipped_samples=" << skipped << " sequence_gaps=" << sequence_gaps
           << " display_drops=" << log.drops;
      log.push(text.str());
      summary = Clock::now() + std::chrono::seconds(1);
      if (trace) trace->on_heartbeat(node.id(), 0);
    }
    log.progress();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  return 0;
} catch (const std::exception& e) {
  std::cerr << "graphx-vita-processor: " << e.what() << '\n';
  return 1;
}
int run_detector(int argc, char** argv) try {
  auto args = node_arguments(argc, argv);
  auto node = load_node_settings(args.config, args.node, "vita.detector");
  stopped = 0;
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  std::signal(SIGPIPE, SIG_IGN);
  const auto& port = node.port("spectra");
  auto cfg = udp(port);
  Socket input(SOCK_DGRAM);
  udp_options(input.fd, cfg);
  if (cfg.max_datagram_bytes < 8836) throw std::runtime_error("power datagram budget");
  bind_socket(input.fd, cfg.bind, cfg.port);
  auto source = address(port.source_address, 0).sin_addr.s_addr;
  auto trace = observer(node);
  Log log;
  if (!await_node_release(node, args, [] { return stopped != 0; })) return 0;
  auto summary = Clock::now();
  std::uint64_t received{}, invalid{}, sequence_gaps{}, duplicates{};
  std::array<std::optional<std::uint32_t>, 4> last{};
  while (!stopped) {
    for (int work = 0; work < 32; ++work) {
      std::array<std::byte, 8837> bytes{};
      sockaddr_in from{};
      socklen_t length = sizeof(from);
      auto n = recvfrom(input.fd, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&from),
                        &length);
      if (n < 0) break;
      ++received;
      try {
        if (from.sin_addr.s_addr != source || n > 8836) throw std::invalid_argument("source/size");
        auto spectrum = decode_spectrum(std::span{bytes}.first(n));
        auto& previous = last[spectrum.stream - 1];
        if (previous) {
          auto delta = spectrum.sequence - *previous;
          if (delta == 0 || delta > 0x80000000u)
            ++duplicates;
          else {
            sequence_gaps += delta - 1;
            previous = spectrum.sequence;
          }
        } else
          previous = spectrum.sequence;
        auto frequency = detect(spectrum);
        std::ostringstream text;
        text.precision(15);
        text << "detection stream=" << spectrum.stream << " sequence=" << spectrum.sequence
             << " seconds=" << spectrum.begin.seconds
             << " picoseconds=" << spectrum.begin.picoseconds << " valid=" << spectrum.valid
             << " size=" << spectrum.size << " rf_hz=";
        if (frequency)
          text << *frequency;
        else
          text << "none";
        log.push(text.str());
      } catch (const std::invalid_argument&) {
        ++invalid;
        log.push("detection invalid");
      }
    }
    if (Clock::now() >= summary) {
      log.push("detector received=" + std::to_string(received) + " invalid=" +
               std::to_string(invalid) + " sequence_gaps=" + std::to_string(sequence_gaps) +
               " duplicates=" + std::to_string(duplicates) +
               " display_drops=" + std::to_string(log.drops));
      summary = Clock::now() + std::chrono::seconds(1);
      if (trace) trace->on_heartbeat(node.id(), 0);
    }
    log.progress();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  log.progress();
  return 0;
} catch (const std::exception& e) {
  std::cerr << "graphx-vita-detector: " << e.what() << '\n';
  return 1;
}
}  // namespace graphx::vita
