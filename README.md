# GraphX

[![CI](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml/badge.svg)](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml)

GraphX 1.1.0 is an educational framework for describing a processing graph once,
running nodes in processes, containers, QEMU, or external runtimes, and observing
traffic across graph edges.

The current configuration format is `version: 2`. GraphX manages Linux network
infrastructure with system Open vSwitch: containers use veth pairs and QEMU guests
use TAP devices. Docker Compose manages application processes and management
connectivity, not the GraphX data plane.

## Five-minute demo

The portable demo needs Docker Engine with Compose on Linux, or OrbStack on macOS.

```sh
scripts/demo.sh start
```

Open the URL printed by the command. Use `scripts/demo.sh logs`,
`scripts/demo.sh token`, and `scripts/demo.sh stop` to inspect or stop it. See
[`docs/complete-system-demo.md`](docs/complete-system-demo.md) for the walkthrough.

## Build and test

Development requires CMake 3.25+, Ninja, OpenSSL 3, and a C++20 compiler.

```sh
scripts/verify.sh quick
```

Available verification profiles are `quick`, `quality`, `sanitizers`, `fuzz`,
`portable`, `full`, `native-linux`, and `release`. Their requirements are documented in
[`docs/test-procedure.md`](docs/test-procedure.md).

For a direct build:

```sh
cmake -S . -B build/dev -G Ninja -DGRAPHX_BUILD_TESTS=ON
cmake --build build/dev -j 4
ctest --test-dir build/dev -L quick --output-on-failure
```

## Configuration and infrastructure

`graphx.yaml` is authoritative. Validate it or produce the normalized JSON consumed
by telemetry and other downstream tools:

```sh
build/dev/graphx validate graphx.yaml
build/dev/graphx config normalize graphx.yaml > normalized.json
```

Preview and manage owned Linux infrastructure with:

```sh
sudo build/dev/graphx infra create graphx.yaml --dry-run
sudo build/dev/graphx infra create graphx.yaml
sudo build/dev/graphx infra status graphx.yaml
sudo build/dev/graphx infra destroy graphx.yaml
```

Cleanup verifies stable resource identities and fails closed when an object has been
replaced or ownership cannot be proven. On macOS, ordinary Compose demos use
OrbStack; privileged Docker/OVS and QEMU/TAP laboratories run in the dedicated
Lima VM. Follow the complete [`macOS Lima/OVS guide`](infrastructure/lima/README.md)
to install, verify, run, inspect, and stop that environment.

## Examples and documentation

Start with [`docs/demo-guide.md`](docs/demo-guide.md). Current examples include the
shared OVS network labs, the [`QEMU TAP lab`](examples/qemu-node/README.md), the
[`SDR scenarios`](examples/sdr-node/README.md), and the
[`route and policy lab`](examples/static-route-policy/README.md).

- [`Architecture`](docs/GraphX_Architecture.md)
- [`Configuration`](docs/configuration.md)
- [`Network infrastructure`](docs/network-infrastructure.md)
- [`macOS Docker and OVS with Lima`](infrastructure/lima/README.md)
- [`Runtime lifecycle`](docs/runtime-lifecycle.md)
- [`Protocol and transports`](docs/protocol.md)
- [`Observability and capture`](docs/observability.md)
- [`Control plane and security`](docs/control-plane.md)
- [`Test procedure`](docs/test-procedure.md)
- [`Release process`](docs/release-process.md)

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before submitting changes. Security and
support contacts are in [`SECURITY.md`](SECURITY.md) and [`SUPPORT.md`](SUPPORT.md).
