# Test procedure

Use the smallest profile that covers the change:

```sh
scripts/verify.sh quick
scripts/verify.sh quality
scripts/verify.sh sanitizers
scripts/verify.sh fuzz
scripts/verify.sh portable
scripts/verify.sh full
scripts/verify.sh native-linux
scripts/verify.sh release
```

- `quick` builds and runs unit and portable contract tests.
- `quality` runs formatting, clang-tidy, and cppcheck on the host toolchain.
- `sanitizers` runs the LLVM 21 address/undefined-behavior suite supported by the host.
- `fuzz` runs bounded LLVM 21 libFuzzer smoke tests.
- `portable` runs complete non-Docker acceptance with C++20.
- `full` adds sanitizers, fuzzing, Docker acceptance, and, when invoked on macOS,
  the Linux Clang 21/libstdc++ 15 quality container.
- `native-linux` runs OVS, veth, namespace, TAP, capture, and fault lifecycles.
- `release` builds and independently verifies a local release candidate from a clean tree.

Run the Linux quality environment directly on any Docker host with:

```sh
scripts/test-linux-container.sh quality
```

Its `quality` mode contains formatting and static analysis only. Fuzzing remains
the separate `fuzz` mode and is not duplicated when `verify.sh full` runs both.
CI uses this digest-pinned verifier image as the single Linux toolchain owner for
quality, sanitizers, and fuzzing; macOS sanitizer acceptance remains native.

Run privileged tests only on native Linux or inside the GraphX Lima guest. Report
host architecture, guest architecture, and QEMU accelerator with results. A passing
test run must leave no newly created GraphX Compose projects, OVS resources,
namespaces, TAP/veth devices, capture processes, qdiscs, or temporary state. Existing workloads must
be preserved. Sealed captures and verification logs are retained evidence.

Portable and full acceptance require Node.js 24.x and fail before installing
dependencies when another Node.js major version is selected. Privileged workflows
are registered as CTests with the `privileged` label; `native-linux` configures that
test set and runs it after portable acceptance.

## Platform selection and example coverage

| Execution environment | Acceptance command | What the result establishes |
|---|---|---|
| Native macOS | `scripts/verify.sh portable` | Native C++/transport and Node.js acceptance |
| OrbStack on macOS | `scripts/test-features.sh docker` | Portable Compose and broadcast; no system OVS |
| Native Linux | `GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/verify.sh native-linux` | Portable acceptance plus privileged Linux CTests |
| GraphX ARM64 Lima on Apple Silicon macOS | `infrastructure/lima/verify.sh` | Guest build/quick tests plus privileged Linux CTests and infrastructure cleanup comparison |

Before Docker tests, check `docker info` and `docker compose version`; on macOS
also check `docker context show` returns `orbstack`. The Docker suite manages the
root Compose project: stop an existing demo deliberately before running it, or
use a separate Compose project and free ports. Before native Linux privileged
CTests, build `graphx-demo:latest` in that engine with `docker build -t graphx-demo:latest .`; allow the network test to
pull `debian:bookworm-slim` and external SDR to build its service image.
Native portable tests also need their configured TCP/UDP/HTTP ports free. Check `node --version` is 24.x before
portable acceptance; another installed major must not be treated as equivalent.

For Lima setup and verification, run on the macOS host:

```sh
infrastructure/lima/start.sh
limactl shell --workdir /workspace/graphx-docker graphx -- \
  docker build -t graphx-demo:latest .
infrastructure/lima/verify.sh
```

A configuration identity mismatch is a failed prerequisite. Follow the guide's
deliberate VM replacement procedure; do not bypass the digest check. Replacement
erases the guest disk. Logs are under `/var/lib/graphx/runtime/evidence` in the
VM; host profile logs are under `outputs/verification`.

Neither `portable` nor `full` runs every example. `portable` runs local TCP,
shared-memory, UDP unicast/multicast, application capture checks, telemetry and
contract tests. `full` adds the standard Compose and UDP broadcast runs, plus
quality/sanitizer/fuzz gates; it does not invoke Lima verification or boot QEMU.
The privileged CTests exercise ownership, container veth, OVS network
profile semantics, TAP lifecycle, capture/faults, external SDR, and static-route policy.
The QEMU TAP CTest exercises infrastructure without booting a guest.

Use the [complete example matrix](../examples/README.md) for launcher commands.
For complete example acceptance, also run the standalone capture launcher,
simulated SDR `start/status/verify/stop`, the isolated Linux broadcast runner,
and QEMU `start/status/verify/stop`. Exercise the macOS network dispatcher for
all four profile names with `plan/up/status/down`. Use an explicit guest shell
for external SDR, static routes, and network observation. Browser interaction
requires the separate [manual checks](manual-test-procedures.md); API checks do
not establish visual acceptance.

Report native Linux, native macOS, OrbStack, and Lima separately, with host and
guest architectures. The checked-in QEMU launcher selects TCG on both Linux and
Lima. Report a successful boot as TCG evidence; TAP tests alone establish neither
TCG guest execution nor KVM. No current example launcher selects KVM.
