# GraphX demonstrations and examples

This guide explains what each runnable scenario demonstrates, how to run it,
what success looks like, and how to clean it up. Commands are run from the
repository root. For formal platform acceptance, use
[`manual-test-procedures.md`](manual-test-procedures.md); for exact negative
tests and low-level diagnostics, use [`test-reference.md`](test-reference.md).

## 1. Choose a demonstration

| Scenario | What it demonstrates | Host | Primary command |
| --- | --- | --- | --- |
| Complete system | Framed TCP graph, telemetry, GUI, control, capture, history | macOS/Linux + Docker | `scripts/demo.sh start` |
| Capture | Exact GraphX frames in USER0 PCAPNG | macOS/Linux | `examples/capture/run.sh` |
| Shared memory | Three processes over bounded POSIX SPSC rings | macOS/Linux | `examples/shared-memory/run.sh` |
| UDP unicast | Five framed loopback datagrams | macOS/Linux | `examples/udp-unicast/run.sh` |
| UDP multicast | Loopback multicast and diagnostic fan-out | macOS/Linux | `examples/udp-multicast/run.sh` |
| UDP broadcast | Directed broadcast confined to a Docker subnet | macOS/Linux + Docker | `examples/udp-broadcast/run.sh` |
| Native UDP broadcast | Disposable namespaces, bridge, capture, decode | Native Linux | `examples/udp-broadcast/run-native-linux.sh` |
| Macvlan | Explicit container IP/MAC identities on one L2 domain | Native Linux | `examples/macvlan/scripts/up.sh` |
| IPvlan L2 | Three routed L2 domains with OVS/SPAN and policy | Native Linux | `examples/ipvlan-l2/scripts/up.sh` |
| IPvlan L3 | Three broadcast-free subnets on one IPvlan parent | Native Linux | `examples/ipvlan-l3/scripts/up.sh` |
| Mixed network | MACVLAN/IPvlan semantics, OVS, nftables, bounded netem | Native Linux or Lima | `examples/mixed-network/scripts/up.sh` |
| External QEMU | Host VM as an observed raw TCP/UDP node | macOS/Linux + Docker | `examples/qemu-node/external/scripts/demo.sh start --accel auto` |
| Container QEMU | Least-privilege nested VM with TCG/KVM proof | Native Linux x86_64 | `examples/qemu-node/container/scripts/demo.sh start --accel auto` |
| Simulated SDR | Raw IQ, mTLS device control, packet history and GUI | macOS/Linux + Docker | `examples/sdr-node/simulated/scripts/demo.sh start` |
| External SDR | Namespace SDR through macvlan and OVS/SPAN | Native Linux | `examples/sdr-node/external/scripts/demo.sh start` |
| Static route/policy | Allowed, denied, missing-route, and route-applied states | Native Linux | `examples/static-route-policy/scripts/demo.sh start` |
| Route/policy inspection | Validation and exact command plans only | macOS/Linux | `examples/static-route-policy/scripts/inspect.sh` |

The root demo is the recommended first run. Local transport examples isolate one
mechanism. Network, QEMU, SDR, and route/policy scenarios are laboratories with
their own lifecycle and evidence boundaries.

## 2. Complete portable system

The standard demo runs this GraphX-aware application:

```text
generator -- Sample(n) --> transform -- TransformedSample(2n) --> sink
       framed TCP edge                 framed TCP edge
              \---- authenticated telemetry ----/
```

Docker Compose runs the three C++ nodes, telemetry service, and browser console
on a private bridge. Capture and SQLite history are enabled by default. Local
observation and control credentials are generated under `.graphx/`.

```sh
scripts/demo.sh start
```

After `start` prints five `PASS` lines, open its printed console URL. Success
means both edges are connected, counters advance, and sink output contains
`value=2*sequence`. Enter the printed token to pause and resume the generator.
Application, Network, and History views should survive navigation, and enabled
capture files should download.

Run these commands individually when needed:

```sh
scripts/demo.sh verify  # Repeat the automated checks.
scripts/demo.sh status  # Show service status and recent sink values.
scripts/demo.sh token   # Print the control token again.
scripts/demo.sh logs    # Follow live output; press Ctrl-C to leave it.
scripts/demo.sh stop    # Stop and remove the demo containers and network.
```

Stopping retains bounded captures/history and the local credential file. See
[`complete-system-demo.md`](complete-system-demo.md) for the full walkthrough
and troubleshooting table.

If port 8080 is already occupied, choose another port. The script uses the same
value for Docker publication, health checks, allowed browser origins, and the
printed console URL:

```sh
GRAPHX_PUBLISHED_HTTP_PORT=18080 scripts/demo.sh start
```

## 3. Local transport and capture examples

Build once before these examples:

```sh
cmake --preset dev
cmake --build --preset dev
```

### 3.1 Application capture

```sh
examples/capture/run.sh
```

Ten messages cross the local TCP graph. The script prints a timestamped
directory containing `generator.pcapng`, `transform.pcapng`, and `sink.pcapng`.
These are `LINKTYPE_USER0` files containing exact `u32be + GXE` frames, not
Ethernet packets. Open them with the checked-in Lua dissector or extcap adapter.
The run is finite and needs no teardown.

### 3.2 Shared memory

```sh
./build/dev/graphx validate examples/shared-memory/graphx.yaml
GRAPHX_BUILD_DIR="$PWD/build/dev" examples/shared-memory/run.sh
```

Generator, transform, and sink are separate host processes connected by two
bounded POSIX shared-memory rings. Success is 20 delivered messages and orderly
exit; listeners unlink their owned segments. This demonstrates one-host SPSC
IPC, backpressure, stale-name recovery, and shutdown rather than networking.

### 3.3 UDP unicast and multicast

```sh
GRAPHX_BUILD_DIR="$PWD/build/dev" examples/udp-unicast/run.sh
GRAPHX_BUILD_DIR="$PWD/build/dev" examples/udp-multicast/run.sh
```

Unicast succeeds with one `PASS received=5`. Multicast succeeds with two such
lines: the modeled subscriber and a diagnostic listener both receive the local
group transmission. These runs prove bounded framed datagrams, not reliability,
authentication, routed multicast, or native one-to-many graph edges.

### 3.4 UDP broadcast

Prepare the local image once, then run the isolated Docker scenario:

```sh
docker build -t graphx-demo:latest .
examples/udp-broadcast/run.sh
```

Success is `PASS received=5`. The fixed internal subnet has no physical parent,
and the cleanup trap removes it. Native Linux can additionally prove namespace
delivery and live GraphX decoding:

```sh
GRAPHX_BUILD_DIR="$PWD/build/dev" GRAPHX_VERIFY_LIVE_CAPTURE=1 \
  examples/udp-broadcast/run-native-linux.sh
examples/udp-broadcast/down-native-linux.sh
```

## 4. Native network laboratories

These examples mutate host networking and require native Linux, Docker Engine,
iproute2, and sudo. OVS/nftables/capture requirements vary by lab. Always review
the dry-run first and stop containers before deleting external networks.

### 4.1 Macvlan

```sh
./build/dev/graphx infra create examples/macvlan/graphx.yaml --dry-run
examples/macvlan/scripts/up.sh
examples/macvlan/scripts/status.sh
docker logs -f gx-mac-sink-sink-1
examples/macvlan/scripts/down.sh
```

The three separately deployed nodes receive explicit IP and MAC addresses on an
isolated dummy-parent L2 domain. Sink output proves the pipeline. The host cannot
directly contact macvlan children without a host-side shim, intentionally absent
from this lab.

### 4.2 IPvlan L2

```sh
./build/dev/graphx infra create examples/ipvlan-l2/graphx.yaml --dry-run
examples/ipvlan-l2/scripts/up.sh
./build/dev/graphx infra create examples/network-observability/graphx.yaml --dry-run
examples/ipvlan-l2/scripts/down.sh
```

Each node occupies an independent L2 domain. Three OVS bridges and one namespace
router provide explicit switching, routing, nftables policy, SPAN, and netem
points. The saved capture is standard Ethernet PCAPNG, unlike the application
capture example.

### 4.3 IPvlan L3

```sh
./build/dev/graphx infra create examples/ipvlan-l3/graphx.yaml --dry-run
examples/ipvlan-l3/scripts/up.sh
examples/ipvlan-l3/scripts/status.sh
examples/ipvlan-l3/scripts/down.sh
```

One IPvlan network owns three subnets on a shared parent. No gateway is declared
and no L2 broadcast crosses the domains. `up.sh` waits up to 60 seconds for an
actual doubled sink value and rolls back on failure.

### 4.4 Mixed network on Linux or Lima

The native topology routes generator traffic from macvlan through OVS, a Linux
namespace router, nftables, and a second OVS bridge into IPvlan L2:

```sh
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
examples/mixed-network/scripts/up.sh
examples/mixed-network/scripts/status.sh
examples/mixed-network/scripts/down.sh
```

On macOS, start Lima and run the same commands in the Linux guest:

```sh
infrastructure/lima/start.sh
limactl shell graphx -- bash -lc \
  'cd /workspace/graphx-docker && examples/mixed-network/scripts/up.sh'
limactl shell graphx -- bash -lc \
  'cd /workspace/graphx-docker && examples/mixed-network/scripts/status.sh'
limactl shell graphx -- bash -lc \
  'cd /workspace/graphx-docker && examples/mixed-network/scripts/down.sh'
infrastructure/lima/stop.sh
```

Both platforms use the same OVS/veth/namespace realization. Results remain
separate native-Linux and Lima evidence rows. See
[`network-infrastructure.md`](network-infrastructure.md).

## 5. QEMU demonstrations

QEMU models an external application that exchanges ordinary TCP/UDP bytes and
does not link GraphX. GraphX provides topology, orchestration, passive packet
observation, bounded Ethernet PCAPNG/history, and scoped control of the origin.

Build the reproducible x86_64 Buildroot guest once:

```sh
examples/qemu-node/scripts/build.sh
```

Run QEMU on a macOS or Linux host while peers and telemetry run in Docker:

```sh
examples/qemu-node/external/scripts/demo.sh start --accel auto
examples/qemu-node/external/scripts/demo.sh verify
examples/qemu-node/external/scripts/demo.sh status
examples/qemu-node/external/scripts/demo.sh token
examples/qemu-node/external/scripts/demo.sh stop
```

On native Linux x86_64, run the VM as a least-privilege container:

```sh
examples/qemu-node/container/scripts/demo.sh start --accel tcg
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh stop
examples/qemu-node/container/scripts/demo.sh start --accel kvm
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh stop
```

Success requires four independently advancing raw edges, QMP VM evidence, fresh
TCP and UDP guest probes, capture/history, and responsive GUI transitions.
Requested, selected, and actual acceleration are separate fields. KVM mode adds
only `/dev/kvm`; TCG is the portable fallback. Follow
[`qemu-demos.md`](qemu-demos.md) for ports, captures, denial tests, and retained
evidence inspection.

## 6. SDR demonstrations

Both SDR profiles implement this external raw graph:

```text
SDR -- UDP IQ --> processor -- TCP result --> sink
SDR <-- mTLS control ----------------------- processor
```

The portable simulation runs entirely through Docker:

```sh
examples/sdr-node/simulated/scripts/demo.sh start
examples/sdr-node/simulated/scripts/demo.sh verify
examples/sdr-node/simulated/scripts/demo.sh control status
examples/sdr-node/simulated/scripts/demo.sh control tune 433920000
examples/sdr-node/simulated/scripts/demo.sh stop
```

Native Linux moves the same simulator into a namespace connected to macvlan and
OVS/SPAN:

```sh
examples/sdr-node/external/scripts/demo.sh start
examples/sdr-node/external/scripts/demo.sh verify
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh stop
```

Success means deterministic IQ reaches the processor, results reach the sink,
mTLS commands change SDR state, raw packet counters advance, and capture/history
remain distinct from GraphX messages. The external profile proves the disposable
native path, not a physical radio. See
[`examples/sdr-node/README.md`](../examples/sdr-node/README.md) before adapting
it to hardware.

## 7. Static route and deny-policy laboratory

This native-Linux laboratory places left, middle, and right endpoints behind
three OVS bridges and one namespace router. It demonstrates four evidence-backed
states: `allowed`, `policy-denied`, `missing-route`, and `route-applied`.

Portable hosts may inspect the validated plans only:

```sh
examples/static-route-policy/scripts/inspect.sh
```

Native Linux runs the state transition:

```sh
examples/static-route-policy/scripts/demo.sh start
examples/static-route-policy/scripts/demo.sh status
examples/static-route-policy/scripts/demo.sh verify
examples/static-route-policy/scripts/demo.sh apply-route
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh stop
examples/static-route-policy/scripts/demo.sh stop
```

The allowed flow requires receiver evidence. The denied flow requires receiver
absence and an advancing named nftables counter. The route transition requires
the exact declared kernel route and receiver result. Mirrored PCAP and GUI state
are corroborating evidence. Cleanup checks run ownership before deleting fixed
names and retains the run directory. Full native acceptance requires two cycles
and the adversarial evidence in
[`phase_14_verification.md`](../phase_14_verification.md).

## 8. Common options, evidence, and cleanup

Interactive `demo.sh` profiles conventionally expose `start`, `verify`,
`status`, `logs`, `token`, and `stop`; SDR adds `control`, and route/policy adds
`apply-route` and `clear-route`. QEMU and SDR accept `--no-capture` and
`--no-history` on `start`. Their GUI ports can be changed with
`GRAPHX_QEMU_GUI_PORT` and `GRAPHX_SDR_GUI_PORT`; the root demo uses its Compose
port configuration.

Use `GRAPHX_BUILD_DIR`, `GRAPHX_MAX_MESSAGES`, and `GRAPHX_INTERVAL_MS` for the
finite native-process examples. Capture artifacts can contain application or
device data and should be handled as sensitive operational evidence.

Never switch privileged labs without running the matching stop/down helper.
When a launcher reports an occupied fixed name or ownership mismatch, inspect
the resource; do not delete or adopt it merely because its name resembles a
GraphX resource. Retained output under `outputs/` or `captures/` is expected;
containers, namespaces, OVS bridges, veths, listeners, and Docker networks are
not.
