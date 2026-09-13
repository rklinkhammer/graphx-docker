# GraphX

The current P1 cutover implements v3 graph validation and normalized JSON version 2.
Graph execution, compilation and unconverted launchers fail explicitly with
`E_PHASE_UNAVAILABLE`. Follow the [current user guide](docs/user-guide.md) and
[phase status](design/graph-generation/p1-verification.md) for the supported workflow.


[![CI](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml/badge.svg)](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml)

GraphX 1.1.0 is an educational framework for describing a processing graph once,
running nodes in processes, containers, QEMU, or external runtimes, and observing
traffic across graph edges.

Start with the [`complete user guide`](docs/user-guide.md) for installation,
configuration, node, transport, networking, capture, and telemetry workflows.

The current configuration format is `version: 3`. GraphX manages Linux network
infrastructure with system Open vSwitch: containers use veth pairs and QEMU guests
use TAP devices. Docker Compose manages application processes and management
connectivity, not the GraphX data plane.

## Validate a graph

```sh
build/dev/graphx validate examples/sample-pipeline/graphx.yml --target orbstack
```

The [configuration guide](docs/configuration.md) explains type instances, catalog
pins, shared endpoint settings, and the current execution gate.

## Build and test

Development requires CMake 3.25+, Ninja, OpenSSL 3, and a C++20 compiler.

```sh
scripts/verify.sh quick
```

Available verification profiles are `quick`, `quality`, `sanitizers`, `fuzz`,
`portable`, `full`, `native-linux`, and `release`. Their requirements are documented in
[`docs/test-procedure.md`](docs/test-procedure.md).

The direct CMake workflow and exact prerequisites are maintained in the
[`complete user guide`](docs/user-guide.md).

## Configuration and infrastructure

Each example owns its `graphx.yml`. Validate the sample pipeline or produce the
normalized JSON consumed by telemetry and other downstream tools:

```sh
build/dev/graphx validate examples/sample-pipeline/graphx.yml
build/dev/graphx config normalize examples/sample-pipeline/graphx.yml > normalized.json
```

Reusable infrastructure modules verify resource identity and fail closed. Their
v3 graph adapter is not yet available. The dedicated Lima environment is reserved
for separately authorized Linux networking and guest verification.

## Examples and documentation

After the complete user guide, use [`docs/demo-guide.md`](docs/demo-guide.md) to
choose a scenario. The [example platform matrix](examples/README.md) identifies
native Linux, native macOS, OrbStack, and Lima requirements for every example.
Current examples include the
shared OVS network labs, the [`QEMU TAP lab`](examples/qemu-node/README.md), the
[`SDR scenarios`](examples/sdr-node/README.md), and the
[`route and policy lab`](examples/static-route-policy/README.md).

- [`Architecture`](docs/GraphX_Architecture.md)
- [`Complete user guide`](docs/user-guide.md)
- [`Configuration contract`](docs/configuration.md)
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
