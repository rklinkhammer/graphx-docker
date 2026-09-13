# P6 execution verification

P6 implements finite native and portable-container execution through `graphx run
up|status|down`, using compiled artifacts, verified release identities and the
existing ownership store. It stages credentials, starts and checks the platform,
collects local application readiness, then atomically publishes the owner-token
release barrier. There is no deployment daemon or restart supervisor.

The accepted clarification is enforced as `E_EXECUTION_MIX`: native and container
applications require separate graphs. OVS, namespace, guest and scenario-action
execution remain gated. No privileged operations were performed.

## Implemented boundary

- Native ownership records include PID, kernel start time, executable path and
  executable hash. Docker records include immutable IDs, owner/graph labels and
  image identity; volumes also record their creation identity. Creation intents
  precede Docker mutation. Cleanup validates the full inventory before stopping
  processes and rejects scope, identity and path substitution.
- Native release installation verifies both archive inventories and matching
  commit/version/platform/epoch, then emits the exact installed file/mode receipt.
  Startup rejects changed executables and unlisted installed files. Container
  startup verifies OCI bytes and release-catalog pins before loading local IDs.
- Native managed output is bounded to 2 MiB plus one previous log; Docker uses
  compiled memory/PID/log limits. Native allocation bounds are not a kernel RSS
  limit. Startup/command waits and interruption recovery are bounded.
- Platform readiness requires valid credentials, HTTP/UDP listeners and ready
  (or disabled) history. A degraded history backend leaves the service live but
  not ready. Routine stop retains history, captures and bounded logs. Docker
  credential volumes and owned process/network resources are removed.
- Portable/native wrappers delegate to `scripts/lib/demo-runtime.sh`; source
  sample/SDR Compose files include generated artifacts. PID loops, hidden endpoint
  defaults and sample-specific startup dependency wiring were removed from those
  launch consumers. Privileged and guest launchers remain gated.
- Discovery publisher/subscriber types use their declared UDP executables and
  schemas. Raw SDR application capture uses PCAPNG USER1 (148), distinct from
  GraphX framed USER0 (147) and Ethernet (1). Per-node captures can be listed and
  downloaded without symlink traversal or fabricated network headers.

## Environments and results

Verification ran on native macOS ARM64 with Node 24 and selected OrbStack
`linux/aarch64`, Docker Compose 5.1.2. Local release candidates use explicit
`--allow-dirty`; they are verified local artifacts, not published release pins.

| Surface | Result and evidence |
|---|---|
| Native S02 shared memory | Startup, traffic, duplicate refusal, busy console, executable identity substitution, interruption, restart and owned stop passed |
| Native S03/S04 UDP unicast/multicast | Same lifecycle checks passed in `outputs/p6-native-udp-unicast.log` and `outputs/p6-native-udp-multicast.log` |
| Native S06 application capture | Lifecycle and three bounded PCAPNG files passed in `outputs/p6-native-capture.log` |
| Installed native release | Verified native archive + pinned Node/web companion installed; lifecycle passed in `outputs/p6-release-native-execution.log` |
| OrbStack S01/S05/S13 | Pipeline, actual broadcast and raw SDR processing/results passed; S13 PCAPNG downloads retained in matrix evidence |
| OrbStack V01–V06 | History, Prometheus/Grafana, control, rotation configuration, secure OTLP and mTLS OTLP baseline startup/traffic/cleanup passed |
| OrbStack T01/T02 | Independent renamed pipelines and both SDR radio/processor/result paths passed |
| Negative/resource checks | Busy console, modified process/image identity, interruption before release, compiled-byte tamper, release inventory tamper, owner-scope/path/duplicate corruption, overlapping runtime/artifact roots, bounded output and capture hardlink refusal covered |
| Portable | 33 CTests, 104 telemetry tests, 17 web tests, production web build and native lifecycle passed |
| Quality | LLVM 21 formatting, clang-tidy and cppcheck passed |
| Packaging | Native candidate's 35 CTests and package inventory passed; each ARM64 image rebuilt twice with matching canonical OCI bytes, inspections and SBOMs |

The Docker matrix records container, network and volume inventories for each
case and keeps a separately owned running sentinel through every case. It checks
that the sentinel remains running and all inventories return unchanged after the
test explicitly disposes its own retained volumes. Captures and logs are copied
to the evidence directory before that disposal. Image caches remain shared.
Native tests retain their private temporary directory on failure for safe recovery.
The installed-release capture run also retains before/after ownership inventories,
process logs and three PCAPNG files in `outputs/p6-native-capture-evidence`.

V04 establishes baseline execution with rotation configured; it does not run the
P9 scenario dispatcher. Rotation overlap/expiry and authenticated controls remain
covered by the portable platform tests. V05/V06 provide disposable private
external credential fixtures; their graph execution results do not claim export
to a real external collector. Portable OTLP tests cover transport/TLS behavior.

## Reproducible commands

Run from the repository root with Node 24 selected:

```sh
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
PATH=/opt/homebrew/opt/node@24/bin:$PATH GRAPHX_UPDATE_COMPILE_GOLDENS=1 GRAPHX_COMPOSE_VALIDATE=1 python3 tests/test_compile.py build/dev/graphx .
```

Compiler verification covered 63 supported target packages, 15 negative inputs,
33 unsupported targets and 51 read-only Compose configurations. Compiler flags
now advertise adapter capability separately from runtime release verification.

```sh
python3 scripts/release/image_release.py build --output outputs/p6-release-images --platform linux/arm64 --allow-dirty
python3 tests/test_execution_matrix.py outputs/p6-release-images --output outputs/p6-release-matrix
python3 scripts/release/build_release.py --build-dir build/p6-release-native --output-dir outputs/p6-release-native --allow-dirty
python3 scripts/release/platform_bundle.py --source . --output outputs/p6-platform-repeat --node-archive outputs/node-v24.20.0-darwin-arm64.tar.gz --epoch "$(git show -s --format=%ct HEAD)" --allow-dirty
python3 scripts/release/install_release.py --native outputs/p6-release-native --companion outputs/p6-platform-repeat --output outputs/p6-release-install --commit "$(git rev-parse HEAD)" --version 1.1.0 --epoch "$(git show -s --format=%ct HEAD)" --allow-dirty
GRAPHX_TEST_RELEASE="$PWD/outputs/p6-release-install" python3 tests/test_execution.py build/dev .
python3 tests/test_install_release.py outputs/p6-release-native outputs/p6-platform-repeat
```

Exact ShellCheck invocation (both source search roots are needed):

```sh
shellcheck -x -P SCRIPTDIR -P . scripts/lib/demo-runtime.sh examples/shared-memory/run.sh examples/udp-unicast/run.sh examples/udp-multicast/run.sh examples/udp-broadcast/run.sh examples/capture/run.sh examples/sample-pipeline/scripts/demo.sh examples/sdr-node/simulated/scripts/demo.sh scripts/test-compose-features.sh scripts/test-telemetry-features.sh
```

## Unrun checks

Native Linux, Linux Docker on a separate host, privileged Linux/Lima, actual QEMU
TCG/KVM guest boot and manual browser interaction were not run. No result here
establishes OVS, namespace, veth/TAP, nftables, netem, network-wire capture or guest
execution. Automated web tests/build and HTTP capture download checks are not
manual browser evidence. P7–P10 retain their own authorization and verification
gates.

## Recorded artifact identities

The accepted image release is `outputs/p6-release-images/images.json`; all three
roles passed duplicate-build canonical OCI equality and image smoke checks. These
are local verified OCI identities; no registry publication is claimed.

| Role | OCI manifest digest |
|---|---|
| runtime | `sha256:0d13020abbe8d9e97a4376a0ec2b56a3efb3c77f2f43f48b6f61e8154f22791a` |
| sdr | `sha256:24961ecc772cdee9e669dda01fb7ac62c416efd9b32b268a60a217109dd13e4c` |
| telemetry | `sha256:e3d8fac4541275f119b68fa7d976ed9a0b9b77b9eb511f95aa3c61cb1b63b04b` |

| Native artifact | SHA-256 |
|---|---|
| `outputs/p6-release-native/graphx-1.1.0-darwin-aarch64.tar.gz` | `1566de297a107c56d6d1aa0253070a2cce0bdb4837d5b3e1c395a0ee4871b1a8` |
| `outputs/p6-platform-repeat/graphx-platform-1.1.0-darwin-aarch64.tar.gz` | `fa542e3aa53a9763df0187109429b72e3c3f2283ce3e742f0911ae877ce0178f` |

The companion archive matched the earlier independent build byte-for-byte.
`outputs/p6-release-install/release.json` records the verified combined inventory.
Wrong commit/epoch, dirty-candidate policy and existing-installation negatives
passed without altering the rejected destination.

Final matrix results are in `outputs/p6-release-matrix/results.json` (11 passes),
with each case's `inventory.json`, process logs and applicable PCAPNG downloads.
The complete portable and quality logs are `outputs/p6-portable.log` and
`outputs/p6-quality.log`. The design checker also passed 24 positive review cases,
15 negatives, 63 target sets and 51 read-only Compose checks; this remains static
design evidence, separate from the runtime matrix. Its inventory now includes
all phase verification documents and links to the current shared Dockerfile and
authored variants.
