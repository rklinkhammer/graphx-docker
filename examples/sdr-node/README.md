# Single-SDR Ethernet example

This suite models the smallest useful SDR graph while keeping ordinary device
traffic outside the GraphX wire protocol:

```text
SDR -- raw UDP IQ blocks --> processor -- raw TCP results --> sink
SDR <-- mutual-TLS TCP control ---------------------------- processor
```

The `simulated` profile runs everywhere Docker runs. The `external` profile is a
native-Linux OVS/SPAN acceptance lab. Both profiles reuse the exact Python SDR,
processor, sink, wire format, TLS control client, packet observer, telemetry
service, GUI, and evidence formats under `common/`. Only attachment and
deployment differ.

| Profile | SDR placement | Ethernet switch | Privilege | Purpose |
|---|---|---|---|---|
| [`simulated`](simulated/README.md) | managed container | isolated Docker bridge (simulation) | Docker only | portable development and macOS/Linux acceptance |
| [`external`](external/README.md) | external Linux namespace simulator | Open vSwitch `br-sdr` with SPAN | host networking via `sudo` | native L2 path and physical attachment contract |

## Wire and security contract

UDP port 18400 carries deterministic `SDR1` sample blocks: a bounded network-
order header followed by at most 256 signed 16-bit I/Q pairs. It is deliberately
ordinary UDP with `framing: none`. UDP is lossy and unauthenticated; the example
does not claim confidentiality, integrity, delivery, ordering, congestion
control, or device authorization. The processor rejects datagrams whose source
address is not the profile's declared SDR address. This reduces accidental or
spoofed cross-talk but is not authentication; the source port remains ephemeral.

TCP port 18401 carries one JSON-line control request/response per mutually
authenticated TLS 1.3 connection. The only actions are `start`, `stop`, `tune`,
and `status`; requests and responses are limited to 4,096 bytes. Demo credentials
are generated locally, expire after seven days, stay under
`examples/sdr-node/.state`, and are not source artifacts. TCP port 18402 carries
bounded JSON-line processor results without GraphX framing.

The GUI token controls the `processor`, which relays pause/resume as stop/start
operations over the authenticated SDR connection. GUI Reset clears collector
counters, matching the standard console; it does not reset the SDR. Control does
not grant GraphX lifecycle ownership of a physical SDR.

## Observability model

Capture and both histories are enabled by default. `tcpdump` produces bounded
classic Ethernet PCAP; the shared QEMU/SDR packet observer safely tails it,
writes link-type-1 PCAPNG, emits `network_packet` telemetry, and maintains a
separate bounded SQLite packet-history database. GraphX message history remains
separate and raw packets are never presented as GraphX envelopes.

Use `--no-capture` or `--no-history` on `start` to disable them. Each run is
retained below `outputs/sdr-node/<profile>/<UTC timestamp>/`; it contains no demo
private keys. Capture payloads can contain sensitive device data and require
normal file access controls.

`--no-capture` disables the derived PCAPNG and GUI capture catalog. The bounded
classic PCAP observation source still exists because live passive metrics and
packet history are derived from it; this matches the QEMU observer model.

The portable capture container retains root only inside its isolated processor
network namespace and has a read-only root filesystem plus only `NET_RAW`,
`NET_ADMIN`, `SETUID`, and `SETGID`. The identity capabilities are needed because
tcpdump's explicit `-Z root` path otherwise fails or drops to an account that
cannot write the mode-0770 host capture directory on Linux.

## Physical SDR contract

Phase 13 does not automatically attach a physical NIC or mutate hardware. To
replace the native namespace simulator, an operator must independently confirm:

- SDR address `10.63.0.10/24`, MAC or port-security requirements, UDP
  destination `10.63.0.20:18400`, and TLS/TCP control listener `18401`;
- processor address `10.63.0.20/24`, sink address `10.63.0.30/24`, interface
  name, MTU/VLAN, switch membership, routing, and firewall policy;
- server certificate identity `sdr-node`, the demo CA trust boundary, client-
  certificate trust, renewal/revocation, and secure private-key provisioning;
- who owns start/stop/reset, interface changes, physical RF safety, and cleanup.

Do not add a production interface to `br-sdr` until its current routes,
addresses, manager, and rollback procedure have been reviewed. The provided
script creates and destroys only the named disposable lab resources. Automated
physical-interface adoption is deferred to the infrastructure ownership phase.

## Source ownership

- `common/protocol.py`: bounded IQ bytes, signed telemetry, control validation.
- `common/sdr_simulator.py`: shared deterministic SDR and TLS server.
- `common/processor.py`: shared UDP processor, TLS controller, GUI relay.
- `common/sink.py`: result endpoint.
- `common/sdrctl.py`: direct authenticated control CLI.
- `../qemu-node/tools/packet_observer.py`: shared raw-packet observation engine;
  profile-specific attribution is supplied through validated rules.

See [ADR 0014](../../docs/adr/0014-external-device-control-cycles.md) for the
external-edge cycle and ownership decision.
