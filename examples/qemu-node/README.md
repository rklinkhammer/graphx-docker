# External x86_64 QEMU TCP/UDP node

This project builds a small x86_64 Linux guest that exchanges ordinary text
messages over TCP and UDP. It does **not** link GraphX, serialize GraphX
envelopes, emit GraphX telemetry, or implement GraphX control commands.

GraphX references the VM as an external node in [`graphx.yaml`](graphx.yaml).
The host peer is shown as origin and receiver facets because configuration
version 1 requires a directed acyclic graph; both facets are one host process.
QEMU captures its virtual Ethernet traffic as PCAP, and `capture_history.py`
indexes bounded packet metadata and payload previews in SQLite. This preserves
the important distinction between network history and GraphX envelope history.

## Selected platform

- Guest architecture: x86_64 (`qemu-system-x86_64`)
- Guest builder: Buildroot 2025.02.17 LTS, pinned by SHA-256
- Guest C library/toolchain: Buildroot musl cross-toolchain
- Emulated NIC: Intel E1000
- Recommended QEMU: 11.1.1; the launcher accepts QEMU 8.2 or newer
- Networking: QEMU user-mode networking, requiring no TAP device or root access

The Docker build environment works on x86_64 Linux and on Apple Silicon through
Docker's `linux/amd64` platform support. Buildroot produces the kernel,
initramfs, and the guest program with its own target cross-compiler.

## Prerequisites

- Docker with Linux containers
- QEMU 8.2 or newer with `qemu-system-x86_64`
- Python 3
- Optional: Wireshark or tshark for interactive PCAP inspection
- A built GraphX CLI if you want to validate or inspect the topology

On macOS with Homebrew:

```sh
brew install qemu
```

On Debian or Ubuntu:

```sh
sudo apt-get install qemu-system-x86 python3
```

## 1. Build the guest

From the repository root:

```sh
examples/qemu-node/scripts/build.sh
```

Build artifacts are written beneath `examples/qemu-node/output/images/` and
downloads beneath `examples/qemu-node/dl/`; both are ignored by Git. The build
does not modify the host toolchain.

Private organization trust uses the same optional inputs as the other GraphX
container builds:

```sh
export GRAPHX_CA_CERT=/absolute/path/to/company-root-ca.crt
export GRAPHX_CERT_INSTALL_SCRIPT=/absolute/path/to/install-certs.sh
examples/qemu-node/scripts/build.sh
```

The certificate and installer are mounted as Docker BuildKit secrets and are
not copied into the resulting guest.

## 2. Validate the topology

```sh
build/dev/graphx validate examples/qemu-node/graphx.yaml
build/dev/graphx inspect examples/qemu-node/graphx.yaml
```

The configuration intentionally has no `deployment.services` section: GraphX
does not start or stop the VM. The TCP/UDP entries describe the four logical
external flows for visualization and operator reference; GraphX transports
must not be attached to these raw payloads.

## 3. Run and capture

```sh
examples/qemu-node/scripts/run.sh
```

The default run lasts 30 seconds. It starts a host peer, boots the guest, tests
host-to-guest and guest-to-host TCP and UDP, and records the virtual Ethernet
traffic. Press `Ctrl-C` to stop early. Override the bounded run when needed:

```sh
examples/qemu-node/scripts/run.sh --duration 120 --max-capture-bytes 67108864
```

Each run creates a timestamped directory under
`outputs/qemu-node/` containing:

- `qemu-node.pcap`: raw Ethernet packets captured by QEMU;
- `packet-history.sqlite`: bounded searchable packet history;
- `guest-console.log`: Linux boot and application logs;
- `host-peer.log` and `probe.log`: peer and connectivity-test logs.

The launcher stops the VM if the PCAP reaches the configured byte ceiling.
History defaults to 10,000 records and 64 payload-preview bytes per record.
Re-index with different limits using:

```sh
python3 examples/qemu-node/tools/capture_history.py \
  outputs/qemu-node/RUN/qemu-node.pcap \
  outputs/qemu-node/RUN/packet-history.sqlite \
  --max-records 5000 --preview-bytes 32
```

Inspect the history:

```sh
python3 examples/qemu-node/tools/query_history.py \
  outputs/qemu-node/RUN/packet-history.sqlite --limit 20
```

Open `qemu-node.pcap` directly in Wireshark. Useful display filters are
`tcp.port == 18001 || tcp.port == 19001` and
`udp.port == 18001 || udp.port == 19001`.

## Network behavior

The guest uses QEMU's conventional `10.0.2.0/24` user network:

| Endpoint | TCP | UDP | Purpose |
|---|---:|---:|---|
| guest `10.0.2.15` | 18001 | 18001 | raw echo services |
| host alias `10.0.2.2` | 19001 | 19001 | host peer echo services |
| host loopback forward | 18001 | 18001 | host-initiated guest tests |

The guest periodically initiates TCP and UDP requests to `10.0.2.2:19001`.
The host probes the guest through QEMU forwards on `127.0.0.1:18001`.

## What appears in GraphX

The topology and its four directed network paths can be displayed. There are
no GraphX message counters, traces, control actions, or application PCAPNG
records because the payload is intentionally not GraphX. Standard PCAP and the
SQLite packet index are the source of truth for this node. A later telemetry
adapter could publish packet summaries to the console without modifying the
guest, but it must not claim those packets are GraphX envelopes.

## Security and limitations

- User-mode networking isolates the guest and exposes only loopback forwards.
- Captures contain complete packet payloads and may contain sensitive data.
- The SQLite index stores bounded payload previews, not full payloads.
- QEMU filter-dump writes classic PCAP rather than PCAPNG.
- The current GraphX deployment schema cannot formally mix managed services and
  externally managed VMs; omitting deployment is the honest representation.
