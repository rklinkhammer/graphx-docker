# Test procedure

`quick` covers the input/target matrix, compiler goldens, application bindings,
release contracts and ownership modules. `portable` also runs telemetry HTTP,
web tests/build and the native execution lifecycle fixture with Node 24.

After confirming the selected engine, run the unprivileged execution matrix:

```sh
PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 tests/test_execution_matrix.py \
  VERIFIED_IMAGE_RELEASE --output FRESH_EVIDENCE_DIRECTORY
```

Use a platform-appropriate Node 24 path on Linux. This runs S01/S05/S13, V01–V06
and T01–T02 using unique graph names and ephemeral console ports. It records
container/network/volume inventories and preserves a separately owned running
sentinel throughout. It explicitly disposes only its own retained test volumes.
The platform-only harness `tests/test_platform_docker.py` remains available for
focused platform/history checks. Neither harness performs privileged operations.

For native lifecycle checks use `python3 tests/test_execution.py build/dev .`.
Append `examples/udp-unicast/graphx.yml`, `examples/udp-multicast/graphx.yml`, or
`examples/capture/graphx.yml` to select other supported native cases. Set
`GRAPHX_TEST_RELEASE` to a verified installation to test actual packaged files;
otherwise the test creates an exact-file local fixture. Set
`GRAPHX_EXECUTION_EVIDENCE` to an absent output directory to retain before/after
ownership inventories, process logs and captures from the first successful run.
Tests retain their private
working directory on failure for identity-checked recovery.

Scenario actions are explicitly selected; see [scenario execution](scenarios.md) and
[P9 verification](../design/graph-generation/p9-verification.md) for portable evidence
and separately authorized networking/guest acceptance. Managed guests require verified artifacts and
separate explicit privileged authorization; see [P8 verification](../design/graph-generation/p8-verification.md). OVS and namespace execution require
explicit privileged opt-in on the local Linux engine and have S07–S12 Lima acceptance evidence; see [P7 verification](../design/graph-generation/p7-verification.md). See
[P6 verification](../design/graph-generation/p6-verification.md) for actual
environment evidence and unrun checks.

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

Run privileged tests only with explicit authorization, on native Linux or inside
the GraphX Lima guest. Report
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
also check `docker context show` returns `orbstack`. The Docker suite uses unique test graph/project names and checks that existing
resources survive unchanged. Before native Linux privileged
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
profile semantics, capture/faults, external SDR, and static-route policy.
QEMU acceptance uses the separate, explicitly authorized S15/T03 harness with
verified guest artifacts; it is not selected by a generic privileged CTest run.

Use the [complete example matrix](../examples/README.md) for launcher commands.
For complete example acceptance, run each of the four network profiles through
`plan/up/status/down` on native Linux, preserving any existing lab workloads.
The checked-in profiles reuse host veth names and must run sequentially. An
existing profile blocks the other profiles on that host until its owner agrees
to stop it with the matching launcher; separate Compose projects are insufficient.
Also run the standalone capture launcher,
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
