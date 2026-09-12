# GraphX

[![CI](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml/badge.svg)](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml)

GraphX is an educational framework for describing a processing graph once,
running its nodes in processes, containers, or external runtimes, and inspecting
what crosses each edge.

GraphX 1.1.0 uses version-2 configuration and system Open vSwitch for mutable
network infrastructure. Containers attach through veth pairs and QEMU guests
through TAP devices. Version 1 remains available only for documented migration.
The current invariants are in
[`docs/project-decisions.md`](docs/project-decisions.md); historical delivery
evidence is under [`docs/archive/`](docs/archive/README.md).

## Five-minute demo

Requirements: Docker Engine with Compose on Linux, or OrbStack with Compose on
macOS, plus `curl`.

```sh
cd ~/workspace/graphx-docker
scripts/demo.sh start
```

The command creates ignored local credentials, builds the portable system, and
checks that traffic reaches the sink. Open the printed console URL (normally
[http://localhost:8080](http://localhost:8080)). Useful follow-up commands are:

```sh
scripts/demo.sh logs
scripts/demo.sh token
scripts/demo.sh stop
```

If port 8080 is busy, set `GRAPHX_PUBLISHED_HTTP_PORT=18080` for the start
command. The full walkthrough and troubleshooting guide is
[`docs/complete-system-demo.md`](docs/complete-system-demo.md).

## Build and test

Local development needs CMake 3.25+, Ninja, OpenSSL 3 development files, and a
C++20 or C++23 compiler. The default build uses C++23.

```sh
scripts/verify.sh quick
```

The test profiles are intentionally separated by purpose:

| Profile | Purpose |
|---|---|
| `quick` | Fast unit and portable contract feedback |
| `portable` | Complete unprivileged feature verification |
| `full` | Local quality checks plus Docker acceptance |
| `privileged-linux` | Native Linux OVS/veth/TAP lifecycle tests |

See [`docs/test-procedure.md`](docs/test-procedure.md) for prerequisites and
commands. Interactive acceptance and cleanup audits are in
[`docs/manual-test-procedures.md`](docs/manual-test-procedures.md).

For a direct build:

```sh
cmake -S . -B build/dev -G Ninja -DGRAPHX_BUILD_TESTS=ON
cmake --build build/dev -j 4
ctest --test-dir build/dev -L quick --output-on-failure
```

## Configuration and runtime model

`graphx.yaml` describes the graph, transports, deployment, network
infrastructure, and observability. The C++ loader is the configuration authority.
Downstream services consume its normalized JSON contract:

```sh
build/dev/graphx validate graphx.yaml
build/dev/graphx config normalize graphx.yaml > normalized.json
```

Mutable version-2 network resources are created through one identity-owned
lifecycle:

```sh
sudo build/dev/graphx infra plan graphx.yaml
sudo build/dev/graphx infra create graphx.yaml
sudo build/dev/graphx infra status graphx.yaml
sudo build/dev/graphx infra destroy graphx.yaml
```

Cleanup verifies stable OVS IDs, interface indexes, namespace/container
identity, process start time, and ownership tokens. It intentionally refuses
name-only deletion. On Apple Silicon, privileged OVS and QEMU work belongs in
the dedicated Lima Linux VM; ordinary containers and portable development use
OrbStack. See [`docs/runtime-lifecycle.md`](docs/runtime-lifecycle.md) and
[`docs/network-infrastructure.md`](docs/network-infrastructure.md).

## Examples

Start with [`docs/demo-guide.md`](docs/demo-guide.md), which compares each
scenario and its evidence boundary. The main examples are:

- `mixed-network`, `macvlan`, `ipvlan-l2`, and `ipvlan-l3`: one shared OVS lab
  topology with different network profiles.
- [`examples/qemu-node/README.md`](examples/qemu-node/README.md): supported
  TAP/OVS QEMU demonstrations and deprecated compatibility profiles.
- [`examples/sdr-node/README.md`](examples/sdr-node/README.md): simulated or
  external radio processing with capture, history, and GUI evidence.
- [`examples/static-route-policy/README.md`](examples/static-route-policy/README.md):
  routing, nftables policy, OVS mirroring, and receiver-confirmed delivery.

The browser-oriented walkthrough is
[`docs/graphical-examples-guide.md`](docs/graphical-examples-guide.md).

## Documentation map

| Need | Source |
|---|---|
| Architecture | [`docs/GraphX_Architecture.md`](docs/GraphX_Architecture.md) |
| Decisions and invariants | [`docs/project-decisions.md`](docs/project-decisions.md) and [`docs/adr/README.md`](docs/adr/README.md) |
| Version-2 configuration | [`docs/configuration-v2.md`](docs/configuration-v2.md) |
| Transport protocols | [`docs/protocol.md`](docs/protocol.md) |
| Observability and capture | [`docs/observability.md`](docs/observability.md) and [`docs/capture.md`](docs/capture.md) |
| Control and security | [`docs/control-plane.md`](docs/control-plane.md) and [`docs/security.md`](docs/security.md) |
| Compatibility and upgrades | [`docs/compatibility-policy.md`](docs/compatibility-policy.md) and [`docs/upgrade.md`](docs/upgrade.md) |
| Releases | [`docs/release-process.md`](docs/release-process.md) |

Markdown is authoritative. Generated DOCX and diagram artifacts are distribution
outputs and should be regenerated for releases rather than hand-edited.

## Release candidates

`VERSION` is the product version. A local, unpublished candidate can be built
and checked with:

```sh
python3 scripts/release/build_release.py \
  --build-dir build/release-local \
  --output-dir outputs/release-local \
  --tag "v$(tr -d '\n' < VERSION)" \
  --allow-dirty
python3 scripts/release/verify_release.py outputs/release-local --source .
```

Published releases must not use `--allow-dirty`. Release archives include the
native package, SPDX SBOM, manifest, and checksums described in the release
process.

## Contributing and support

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before submitting changes. Report
security issues through [`SECURITY.md`](SECURITY.md); other support paths are in
[`SUPPORT.md`](SUPPORT.md). Third-party dependency details are recorded in
[`THIRD_PARTY.md`](THIRD_PARTY.md).
