# Test procedure

`quick` covers the input/target matrix, compiler goldens, application bindings,
release contracts and ownership modules. `portable` also runs telemetry HTTP,
web tests/build and the native execution lifecycle fixture with Node 26.

After confirming the selected engine, run the unprivileged execution matrix:

```sh
PATH=/opt/homebrew/bin:$PATH python3 tests/test_execution_matrix.py \
  VERIFIED_IMAGE_RELEASE --output FRESH_EVIDENCE_DIRECTORY
```

Use a platform-appropriate Node 26 path on Linux. This runs S01/S05/S13, V01–V06
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

Scenario actions are explicitly selected; see [scenario execution](user-guide.md#scenario-actions) and
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
  Instrumented GraphX targets use `-O1` and retain frame pointers so the complete
  configuration and lifecycle matrices fit their test deadlines on libstdc++.
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

P3 jumbo veth paths additionally require `ethtool` on the Linux host/guest.
The common lifecycle disables and verifies TSO/GSO/GRO and transmit checksum
offload on both veth ends so diagnostic capture sees complete MTU-bounded frames.
The Lima provisioning package list includes this prerequisite; changing the list
does not provision or authorize a guest run.

Run privileged tests only with explicit authorization, on native Linux or inside
the GraphX Lima guest. Report
host architecture, guest architecture, and QEMU accelerator with results. A passing
test run must leave no newly created GraphX Compose projects, OVS resources,
namespaces, TAP/veth devices, capture processes, qdiscs, or temporary state. Existing workloads must
be preserved. Sealed captures and verification logs are retained evidence.

Portable and full acceptance require Node.js 26.x and fail before installing
dependencies when another Node.js major version is selected. Privileged workflows
are registered as CTests with the `privileged` label; `native-linux` configures that
test set and runs it after portable acceptance.

For the pinned LLVM 21 toolchain on macOS, `verify.sh` probes `<random>` against
the selected SDK before quality, sanitizer or fuzz configuration. If the active
SDK requires newer Clang resource headers, the launcher verifies and selects an
installed macOS 26 SDK. An explicit `SDKROOT` is respected and must pass the probe.
This selects compatible headers; it does not suppress analysis or redefine system
macros. Direct `run-static-analysis.sh`/`run-fuzz.sh` users should supply the same
compatible `SDKROOT`. The opt-in VITA sanitizer commands and scope are recorded in
[P1 qualification](../design/four-radio-vita/p1-verification.md).

## Platform selection and example coverage

| Execution environment | Acceptance command | What the result establishes |
|---|---|---|
| Native macOS | `scripts/verify.sh portable` | Native C++/transport and Node.js acceptance |
| OrbStack on macOS | `GRAPHX_IMAGE_RELEASE=VERIFIED_IMAGES scripts/test-features.sh docker` | Compiled portable container matrix; no system OVS |
| Native Linux | `GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/verify.sh native-linux` | Portable acceptance plus privileged Linux CTests |
| GraphX ARM64 Lima on Apple Silicon macOS | `infrastructure/lima/verify.sh` | Guest build/quick tests plus privileged Linux CTests and infrastructure cleanup comparison |

Before Docker tests, check `docker info` and `docker compose version`; on macOS
also check that `docker context show` returns `orbstack`. Use a verified image
release with `GRAPHX_IMAGE_RELEASE`; test graphs use unique identities and preserve
existing workloads. Node.js 26 is required for portable checks.

The privileged CTest `graphx-compiled-live` runs the six compiled OVS cases,
S11/S12/S14 scenario actions, and actual S15/T03 TCG guests sequentially. Set:

```sh
export GRAPHX_ALLOW_PRIVILEGED_TESTS=1
export GRAPHX_IMAGE_RELEASE=/var/lib/graphx/VERIFIED_IMAGES
export GRAPHX_TEST_RELEASE=/var/lib/graphx/VERIFIED_INSTALLATION
export GRAPHX_GUEST_RELEASE=/var/lib/graphx/VERIFIED_GUESTS
scripts/verify.sh native-linux
```

For the existing GraphX Lima VM, use the same variables with guest-local paths and
run `infrastructure/lima/verify.sh` on macOS. It checks VM identity, builds/tests in
guest storage, and compares before/after infrastructure inventories. It neither
provisions a VM nor builds topology-specific images. The direct guest entry point
is `GRAPHX_TEST_TARGET=lima scripts/test-linux-network-features.sh`.

A configuration identity mismatch is a failed prerequisite. Follow the Lima guide's
explicit replacement procedure only when separately authorized; replacement erases
the guest disk. Do not bypass the identity check.

Neither `portable` nor `full` runs every example. `full` adds the portable container
matrix, quality, sanitizers and fuzzing. The separately authorized privileged suite
adds OVS, route/fault actions and actual QEMU guests. The [example matrix](../examples/README.md)
identifies each authored input and target; [P10 verification](../design/graph-generation/p10-verification.md)
records actual environment coverage. Browser interaction requires the separate
[manual checks](user-guide.md#manual-acceptance-checks); API checks do not establish visual acceptance.

Report native Linux, native macOS, OrbStack and Lima separately, including host and
guest architectures. The accepted guest cases use x86_64 TCG. KVM and physical-radio
startup are separate, unimplemented contracts.


## Test families
The CTest suite is grouped by purpose:

| Label | Coverage |
|---|---|
| `quick` | C++ units, configuration, normalized schema, scripts, and documentation |
| `stress` | repeated transport lifecycle and concurrency pressure |
| `package` | installation, archive contents, consumer linkage, release contracts |
| `privileged` | Linux (native or Lima) OVS, veth/TAP, namespaces, capture, faults, SDR, and routes |

Telemetry uses Node's test runner. Docker scripts exercise the portable end-to-end
system, operations stack, secure OTLP, history, and hardening. Privileged Python
tests exercise current OVS ownership, container veth, network labs, QEMU TAP,
network capture/fault behavior, SDR delivery, and route policy. CTest fixtures own
setup and cleanup for multi-step laboratories.

## Four-radio VITA integrated acceptance

P6 uses `tests/test_vita_acceptance.py` with the maintained example and a verified
normal `--with-vita` image release (qualification hooks OFF). Build images fresh
once per verification run and stage them in the identity-matched Lima guest. Native
observer rejection tests run as `graphx-vita-acceptance-contract` in quick/portable.
The harness's preparation mode verifies and compiles only; `--run` additionally
requires Linux root and explicit `--allow-privileged` authorization.

In the guest, use an existing current-source CLI and absent run identity:

```sh
sudo python3 tests/test_vita_acceptance.py --cli "$GRAPHX_CLI" \
  --images /var/lib/graphx/verification/p6/images-recovery \
  --workspace /var/lib/graphx/verification/p6/examples \
  --output /var/lib/graphx/verification/p6/run6 --case baseline
```

After explicit authorization, append `--run --allow-privileged --seconds 180`.
For an authorized native Linux host, also select `--target native-linux`; the
default target is `lima`. Evidence records the selected target.
Run the `radio1`, `detector`, `processor` and `recorder` cases with the same selected
images and run directory; every case creates new containers. Each fault case checks
explicit whole-graph recovery with another new container set. `--browser` adds
bounded baseline/degraded checkpoints: inspect the authenticated console and write
the matching `browser-LABEL.done` only after completing the UI check. A timeout fails
acceptance and triggers owned cleanup. Never credit a checkpoint file by itself as
browser evidence. Use a fresh output identity to retry a failed case.

The harness records independent packet/timestamp/quality observations, measured
rate, RSS samples, faults, stable identities, preserved sentinel and cleanup.
Source skips, observer drops and receiver gaps are distinct. See the
[acceptance design](../design/four-radio-vita/acceptance-design.md) and
[requirement matrix](../design/four-radio-vita/verification.md) for thresholds,
exact results and limitations. No physical-radio or QEMU execution is implied.
