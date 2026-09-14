# GraphX

GraphX accepts authored v3 graphs, resolves normalized JSON contract 2, and
compiles deterministic artifacts. Verified releases support finite native and
unprivileged container execution with owned cleanup. OVS and guest execution require explicit Linux privileged opt-in; scenario actions
are selected explicitly. Follow the [user guide](docs/user-guide.md),
[execution guide](docs/execution.md), and [phase status](design/graph-generation/implementation-plan.md).


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

## Run your first example

Follow the [example quick start](examples/quick-start.md) for copyable commands to
build shared images, compile the sample pipeline, start it, open the console and
stop it. The same sequence runs the simulated SDR and UDP broadcast examples.

Example launchers require a compiled graph and explicit paths; running a launcher
alone does not build or configure the example.

## Validate a graph

```sh
graphx example up sample-pipeline --control generator:pause,resume
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

Reusable infrastructure modules verify resource identity and fail closed. The compiled v3 runner uses those modules. The dedicated Lima environment is reserved
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
