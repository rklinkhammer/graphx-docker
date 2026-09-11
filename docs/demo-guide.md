# GraphX demonstrations and examples

This guide explains what each runnable scenario demonstrates, how to run it,
what success looks like, and how to clean it up. Commands are run from the
repository root. For formal platform acceptance, use
[`manual-test-procedures.md`](manual-test-procedures.md); for exact negative
tests and low-level diagnostics, use [`test-reference.md`](test-reference.md).

## 1. Choose a demonstration

| Scenario | What it demonstrates | Host | Primary command |
| --- | --- | --- | --- |
| Complete system | Framed TCP graph, telemetry, GUI, control, capture, history | macOS + OrbStack / Linux + Docker | `scripts/demo.sh start` |
| Capture | Exact GraphX frames in USER0 PCAPNG | macOS/Linux | `examples/capture/run.sh` |
| Shared memory | Three processes over bounded POSIX SPSC rings | macOS/Linux | `examples/shared-memory/run.sh` |
| UDP unicast | Five framed loopback datagrams | macOS/Linux | `examples/udp-unicast/run.sh` |
| UDP multicast | Loopback multicast and diagnostic fan-out | macOS/Linux | `examples/udp-multicast/run.sh` |
| UDP broadcast | Directed broadcast confined to a Docker subnet | macOS/Linux + Docker | `examples/udp-broadcast/run.sh` |
| Native UDP broadcast | Disposable namespaces, bridge, capture, decode | Native Linux | `examples/udp-broadcast/run-native-linux.sh` |
| MACVLAN semantics | Explicit container IP/MAC identities on one OVS L2 domain | Native Linux/Lima | `scripts/network-lab.sh macvlan up` |
| IPVLAN L2 semantics | Three routed OVS L2 domains and namespace policy | Native Linux/Lima | `scripts/network-lab.sh ipvlan-l2 up` |
| IPVLAN L3 semantics | Three routed subnets with broadcast-free profile behavior | Native Linux/Lima | `scripts/network-lab.sh ipvlan-l3 up` |
| Mixed semantic network | MACVLAN-to-IPVLAN routing, OVS, and nftables | Native Linux/Lima | `scripts/network-lab.sh mixed-network up` |
| Canonical QEMU | Unprivileged VM on a GraphX-owned OVS TAP | Native Linux/Lima | `examples/qemu-node/scripts/demo.sh start` |
| External QEMU compatibility | Host VM plus telemetry GUI over user networking | macOS/Linux + Docker | `examples/qemu-node/external/scripts/demo.sh start --accel auto` |
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
printed console URL. On macOS, port 18080 is reserved by the GraphX Lima
configuration for forwarding a service listening on guest port 8080, so 28080
is the recommended alternate while the Lima VM is running:

```sh
GRAPHX_PUBLISHED_HTTP_PORT=28080 scripts/demo.sh start
```

If a healthy container cannot be reached through `127.0.0.1`, stop the demo,
restart OrbStack, and retry. A stale SSH tunnel or another listener on the
selected port must be stopped or assigned a different local port first.

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

## 4. System-OVS network laboratories

These examples mutate Linux networking and require Docker Engine, iproute2,
system OVS, and sudo. On native Linux the cross-platform dispatcher runs the
canonical launcher locally. On Apple Silicon macOS it runs the same launcher
inside the identity-checked GraphX Lima VM; OrbStack is not involved. Prepare
Lima once before the first macOS run:

```sh
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
```

If `start.sh` refuses an older instance because its recorded configuration
digest is stale, preserve any required VM-local evidence and follow the
deliberate replacement procedure in
[`infrastructure/lima/README.md`](../infrastructure/lima/README.md#deliberate-reset-or-removal).
The lifecycle never deletes a VM automatically.

The canonical dispatcher and the individual example `up.sh`, `status.sh`, and
`down.sh` entry points share the same runtime policy. You may invoke either
form: privileged operations stay local on Linux and enter Lima on macOS.

Always run `plan` first. The dispatcher selects the VM-native GraphX executable
on macOS, so a macOS `build/dev/graphx` binary is never executed in Linux.

### 4.1 Macvlan

```sh
scripts/network-lab.sh macvlan plan
scripts/network-lab.sh macvlan up
scripts/network-lab.sh macvlan status
scripts/network-lab.sh macvlan down
```

The three managed containers receive explicit IP and MAC addresses through
GraphX-owned veth pairs on one OVS L2 domain. Sink output proves the pipeline;
no Docker macvlan driver or physical parent is involved.

### 4.2 IPvlan L2

```sh
scripts/network-lab.sh ipvlan-l2 plan
scripts/network-lab.sh ipvlan-l2 up
scripts/network-lab.sh ipvlan-l2 status
scripts/network-lab.sh ipvlan-l2 down
```

Each node occupies an independent L2 domain. Three OVS bridges and one namespace
router provide explicit switching, routing, and nftables policy. Use the focused
network-observability example for declarative SPAN capture and timed netem
faults.

### 4.3 IPvlan L3

```sh
scripts/network-lab.sh ipvlan-l3 plan
scripts/network-lab.sh ipvlan-l3 up
scripts/network-lab.sh ipvlan-l3 status
scripts/network-lab.sh ipvlan-l3 down
```

One IPvlan network owns three subnets on a shared parent. No gateway is declared
and no L2 broadcast crosses the domains. `up.sh` waits up to 60 seconds for an
actual doubled sink value and rolls back on failure.

### 4.4 Mixed semantic network

The topology routes generator traffic from a MACVLAN-semantic OVS domain
through a Linux namespace router and nftables into an IPVLAN-L2-semantic OVS
domain. Run it on native Linux or inside the GraphX Lima guest:

```sh
scripts/network-lab.sh mixed-network plan
scripts/network-lab.sh mixed-network up
scripts/network-lab.sh mixed-network status
scripts/network-lab.sh mixed-network down
```

OrbStack supplies unprivileged Compose demos on macOS but is not a privileged
network-lab backend. Declarative fault and capture behavior is demonstrated by
`examples/network-observability`. See
[`network-infrastructure.md`](network-infrastructure.md).

## 5. QEMU demonstrations

QEMU models an external application that exchanges ordinary TCP/UDP bytes and
does not link GraphX. GraphX provides topology, orchestration, passive packet
observation, bounded Ethernet PCAPNG/history, and scoped control of the origin.

Build the reproducible x86_64 Buildroot guest once:

```sh
examples/qemu-node/scripts/build.sh
```

The first build downloads Buildroot sources and compiles an x86_64 cross
toolchain, so it can take substantially longer than later builds and requires
working access to Docker Hub and the upstream source mirrors.

On native Linux, provision the fixed unprivileged QEMU identity once. These
commands fail closed if UID/GID 65532 already belongs to another identity:

```sh
getent group graphx-qemu >/dev/null || sudo groupadd --system --gid 65532 graphx-qemu
getent passwd graphx-qemu >/dev/null || sudo useradd --system --uid 65532 --gid graphx-qemu --home-dir /var/lib/graphx/qemu --shell /usr/sbin/nologin graphx-qemu
sudo install -d -o graphx-qemu -g graphx-qemu -m 0750 /var/lib/graphx/qemu
```

Run the canonical TAP/OVS demo. The dispatcher runs it directly on Linux or in
the identity-checked GraphX Lima VM on Apple Silicon macOS:

```sh
examples/qemu-node/scripts/demo.sh start
examples/qemu-node/scripts/demo.sh status
examples/qemu-node/scripts/demo.sh pause
examples/qemu-node/scripts/demo.sh resume
examples/qemu-node/scripts/demo.sh verify
examples/qemu-node/scripts/demo.sh stop
```

This path proves the unprivileged QEMU identity, TAP ownership, OVS switching
and SPAN, VLAN isolation, TCP/UDP guest readiness, and QMP pause/resume. Runtime
evidence stays in the Lima/native-Linux filesystem.

The deprecated external compatibility profile remains useful for exercising
the telemetry GUI with a host QEMU process and user-mode networking:

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
examples/sdr-node/external/scripts/demo.sh up
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh down
```

For the portable profile, success means deterministic IQ reaches the processor,
results reach the sink, mTLS commands change SDR state, raw packet counters
advance, and capture/history remain distinct from GraphX messages. The external
profile instead proves the disposable native OVS attachments, SPAN topology,
simulator namespace, and unprivileged processor/sink processes; it is not a
physical-radio or telemetry-GUI profile. See
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
examples/static-route-policy/scripts/demo.sh up
examples/static-route-policy/scripts/demo.sh status
sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.2.10
! sudo ip netns exec gx-route-middle-end ping -c 1 -W 1 10.64.1.10
! sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
sudo ip netns exec gx-route-router nft list chain inet graphx forward
examples/static-route-policy/scripts/demo.sh apply-route
sudo ip netns exec gx-route-router ip route show 10.64.30.10/32
sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh down
```

The first ping proves the allowed path. The second must fail while advancing the
named `deny-middle-left` nftables counter, and the third must fail because the
manual route is absent. After `apply-route`, inspect the exact declared kernel
route and require the final ping to succeed. Cleanup checks ownership before
deleting fixed names. Historical GUI, receiver, and capture evidence from the
retired pre-M8 launcher remains under `docs/archive/` and is not produced by
this current infrastructure laboratory.

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
