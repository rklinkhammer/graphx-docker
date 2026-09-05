# Phase 12 Independent Verification Report

**Verdict: BLOCKED**

**Verification date:** 2026-09-05  
**Repository:** `/Users/rklinkhammer/workspace/graphx-docker`  
**Branch / baseline:** `main` / `7cbb2ce85d677af3c592446a33c62352527b7982` (`Qemu`)  
**Verified state:** uncommitted Phase 12 working tree; no product code changed by this verification  
**Contracts:** repository `prompt/implement.md` (372 lines) and `prompt/verifier.md` (396 lines), byte-identical to the reviewed copies in `/Users/rklinkhammer/workspace/prompt`

## 1. Executive summary

The three findings in the preceding report are remediated. Independent static,
automated, API, CLI, real-QMP, and live-browser checks confirmed that:

- the continuous monitor refreshes QMP state and survives a VM pause;
- VM, guest application, and TCP/UDP readiness now change independently and
  accurately through running, paused/unavailable, recovered, and stopped
  states;
- the GUI Network view retains and displays those distinct states, remains
  populated after History navigation, and updates without refresh; and
- the container profile rejects non-Linux hosts before port, artifact, or
  Docker preflight, with exit 2 and an actionable diagnostic.

No P0, P1, P2, or P3 product finding remains from the work exercised on this
host. The external profile is runtime verified on macOS/arm64 using the shared
x86_64 guest under QEMU TCG. The raw topology, artifact reuse, packet
observation, Ethernet PCAPNG, separate packet history, scoped control,
bounded lifecycle, security declarations, and compatibility suites passed.

The verdict is nevertheless **BLOCKED**, not `ACCEPTED`, because the governing
contract requires native-Linux runtime proof for the container TCG and KVM
profiles. By agreement, those checks will be run manually by the operator.
Compose rendering, image building, mock QMP/KVM checks, and macOS Docker
inspection do not substitute for a real Linux guest boot, real KVM activation,
live device/group permissions, signal behavior, networking, or cleanup.

## 2. Evidence classification

- **Runtime verified — external profile:** real x86_64 guest under host
  QEMU/TCG on macOS; supporting services in Docker.
- **Automated test with simulated dependency:** mock QMP, PCAP fixtures,
  configuration adversarial cases, telemetry, GUI model, history, capture,
  lifecycle, and security tests.
- **Inspection only:** Linux Compose models, KVM overlay, runtime image,
  healthcheck, entrypoint, and container hardening declarations.
- **Not verified — environmental restriction stated:** native Linux external
  host-gateway path, container TCG runtime, container KVM runtime, `/dev/kvm`,
  exact LLVM 18 quality tools, and macOS libFuzzer execution.
- **Failed:** none attributable to the implementation in this verification.

The implementation handoff was treated as a test lead, not evidence.

## 3. Environment and change audit

| Item | Independently observed value |
|---|---|
| Host | macOS 26.6.2 (25G83), Darwin 25.6.0, arm64, Apple M4 Max |
| Memory / workspace storage | 64 GiB RAM; 210 GiB available |
| Docker | client/server 29.4.0; Compose 5.1.2 |
| QEMU | 11.1.1, `qemu-system-x86_64` |
| Build tools | CMake 4.4.3; Ninja 1.13.2; Apple Clang 21.0.0 |
| Application tools | Node 26.8.1; npm 11.19.0; Python 3.13.5/3.14.7 |
| Packet/data tools | TShark 4.6.8; SQLite 3.51.0 |
| Unavailable | native Linux, `/dev/kvm`, `clang-format-18`, `clang-tidy-18`, macOS libFuzzer runtime |
| Repository state | dirty before verification with 30 modified tracked paths and 27 untracked Phase 12 paths; unrelated work preserved |
| Diff hygiene | `git diff --check` passed before report replacement |

The reviewed change is focused on Phase 12: additive node/runtime and raw-edge
configuration, a shared QEMU suite with two thin profile wrappers, one guest,
one endpoint implementation, one packet observer, telemetry/GUI extensions,
tests, and documentation. No TAP, host bridge mutation, guest broadcast or
multicast, physical SDR behavior, or arbitrary external-node orchestrator was
introduced.

Generated Buildroot output and runtime evidence are ignored. Final cleanup
left no Phase 12 container, Docker network, QMP socket, or owned PID marker.
Retained captures, history databases, logs, and evidence remain under the
documented `outputs/qemu-node` tree.

## 4. QEMU-001 through QEMU-017 traceability

| ID | Implementation evidence | Independent validation evidence | Profile/platform | Status | Remediation |
|---|---|---|---|---|---|
| QEMU-001 | `external/graphx.yaml`, `container/graphx.yaml`; four required IDs | Config CTests and normalized equivalence/adversarial suite passed | Both / automated | **PASS** | None |
| QEMU-002 | `data_plane: external`, `framing: none`; factory rejection in `src/transport_factory.cpp` | Raw positive/negative cases, framed compatibility, API serialization, and real packet inspection passed | Both / automated + external runtime | **PASS** | None |
| QEMU-003 | One guest tree, peers, observer, GUI, manifest, and payload contract | Current source/artifact manifest verified; tamper tests passed; both profiles reference the same artifacts | Both | **PASS** | Repeat clean dual build for release provenance if desired |
| QEMU-004 | Host/external QEMU metadata and owned host processes | Real API, process command, QMP socket, CLI, and browser checks | External / macOS TCG | **PASS** | Repeat Linux host-gateway path |
| QEMU-005 | Host launcher, QMP proof, dual guest probes, four raw paths | Guest boot, actual TCG, bidirectional TCP/UDP, verify/status/logs/token/control, repeat lifecycle, and cleanup passed | External / macOS TCG | **PASS** | External Linux remains operator evidence |
| QEMU-006 | Managed QEMU service plus container → VM → guest hierarchy | Config/API/GUI tests, Compose render, image and entrypoint inspection; no Linux runtime | Container / inspection | **PARTIAL** | Run container profile on native Linux |
| QEMU-007 | TCG base model, KVM device/group overlay, QMP `query-kvm`, non-root image | Mock proof/denial and static hardening passed; runtime image built as 65532:65532 | Container / inspection | **NOT RUNTIME VERIFIED** | Run native Linux TCG, KVM, denial, and auto gates |
| QEMU-008 | Bounded QMP/readiness monitor, owned PID checks, graceful shutdown/fallback | External start, real pause/recovery, unrelated-PID survival, idempotent stop, immediate restart, and cleanup passed | External runtime; container simulated/inspection | **PASS externally / PARTIAL container** | Complete Linux signal/restart/interruption gates |
| QEMU-009 | `network_packet` observation and API/WebSocket counters | All four edge counters advanced; browser sample count advanced 21 → 22 without refresh | External runtime | **PASS externally / Linux pending** | Repeat container runtime |
| QEMU-010 | Default bounded source PCAP/PCAPNG and secure catalog/download | Real Ethernet PCAPNG with TCP/UDP; security and malformed-file tests; `--no-capture` passed | External runtime + automated | **PASS externally / Linux pending** | Repeat container runtime |
| QEMU-011 | Separate bounded SQLite packet history and API | Real records and required fields; retention/migration/query/restart tests; `--no-history` returned 503 | External runtime + automated | **PASS externally / Linux pending** | Repeat container runtime |
| QEMU-012 | Shared GUI and distinct boundary/VM/guest nodes with live fields | Application/Network/History/Capture transitions, History → Network, QMP pause/recovery, and WebSocket update passed; container model tests passed | External runtime; container simulated | **PASS externally / PARTIAL container** | Exercise live nested model on Linux |
| QEMU-013 | `host-origin` alone has control capability; guest control is `none` | Unauthenticated 401; pause/resume 202; reset 200; origin high-rate traffic stopped/restarted; no guest control shown | External runtime + automated | **PASS externally / Linux pending** | Repeat container runtime |
| QEMU-014 | One 570-line implementation behind two four-line wrappers | Shared verbs/help; external workflow passed; invalid accelerator deterministic; non-Linux container check returned 2 before invalid-port preflight | Both | **PASS externally / PARTIAL container** | Run all verbs on Linux container profile |
| QEMU-015 | Private/loopback publications, private QMP, bounded storage/parser/waits/logs; hardened Compose | Base render has no privilege, device, capability, Docker socket, host network, TUN, or NET_ADMIN; external exposure and mode checks passed | External runtime; container inspection | **PASS externally / PARTIAL container** | Inspect live Linux KVM user/groups/devices/caps/mounts/ports |
| QEMU-016 | Additive compatibility and comprehensive regression hooks | Fresh and portable C++20/C++23 28/28, package, TLS, UDP, telemetry 76/76, GUI 13/13, sanitizers 28/28 | Portable / macOS | **PARTIAL** | Run exact LLVM 18 and executable fuzz gates on supported Linux toolchain |
| QEMU-017 | Common guide, profile READMEs, ADR, root and graphical guides | External procedure followed literally; Linux procedure inspected and remains explicit | Both | **PASS** | Operator should record any Linux-specific corrections |

## 5. Shared architecture and profile-specific assessment

Both profiles expose the same acyclic logical topology:

```text
host-origin -- ordinary TCP + UDP --> qemu-node
qemu-node   -- ordinary TCP + UDP --> host-receiver
```

All four edges are raw/external with no GraphX length frame. Captured payloads
were deterministic ordinary bytes such as `host tcp sequence=...` and
`host udp sequence=...`; they were not GraphX envelopes. The native loader
rejects invalid raw/framed combinations and the transport factory rejects raw
edge construction.

Reuse is genuine rather than duplicated: one Buildroot tree and guest source,
one artifact manifest, one `peer.py`, one relay, one observer, one telemetry
service, one GUI, and one `demo-profile.sh`. External and container scripts are
four-line forwarding wrappers. Profile YAML/Compose files contain the intended
ownership and endpoint differences.

- **External:** Docker supporting services, host QEMU, host observer, private
  host QMP socket.
- **Container:** Docker-managed QEMU/relay/observer, with a modeled container,
  nested VM, and guest application. Runtime proof remains Linux-only.

## 6. Resolution of preceding findings

### F-012-06 — resolved

`examples/qemu-node/tools/qmp_control.py:128-169,234-275` now maps bounded
`query-status` refreshes and prevents guest probes from overwriting a
non-running VM. The monitor stays alive while QMP reports paused or temporarily
unavailable. Mock QMP loss/pause/recovery tests passed. In a real QEMU run:

```text
ready:   VM=running guest=ready       TCP=true  UDP=true
paused:  VM=paused  guest=unavailable TCP=false UDP=false
resumed: VM=running guest=ready       TCP=true  UDP=true
```

CLI, API, and GUI agreed, and readiness PID 4034 survived both transitions.

### F-012-07 — resolved

`apps/telemetry/server.mjs:211-235`, `web/src/data/topology.js:28-63`,
`web/src/App.jsx:77-82`, and `web/src/components/NodeCard.jsx:8-13` preserve and
render independent boundary, VM, guest, and protocol state. Live browser
verification showed host boundary `running`, VM `paused`, guest `unavailable`,
TCP/UDP `down`, then full recovery. History → Network remained populated.
Automated booting/ready/degraded/recovered/stopped model tests passed.

### F-012-08 — resolved

`examples/qemu-node/scripts/demo-profile.sh:400-405` rejects the Linux-only
profile before Docker, port, and artifact checks. On macOS, a container start
with `GRAPHX_QEMU_GUI_PORT=not-a-port` returned exit 2 and only the native-Linux
diagnostic.

## 7. Findings

No new P0, P1, P2, or P3 implementation finding was identified.

The following are evidence limitations or pre-existing quality debt, not
implementation findings:

- native Linux and `/dev/kvm` are unavailable in this environment;
- the production GUI build retains its existing greater-than-500-KiB bundle
  warning;
- the exact versioned LLVM 18 format/static-analysis tools are absent; and
- Apple Clang cannot link the configured fuzz targets because
  `libclang_rt.fuzzer_osx.a` is absent.

During Compose inspection, the verifier first omitted the required
`GRAPHX_KVM_GID` from a manual KVM-overlay render. Repeating with an explicit
inspection value passed. This was verifier setup error, not a product failure.

## 8. Exact commands and results

| Command/check | Result |
|---|---|
| Fresh CMake configure/build in `build/phase12-final-verification-20260905` and complete CTest | **28/28 passed**, 15.67 s test time |
| `scripts/verify.sh portable` | **Passed** C++23/C++20 28/28, topology, TCP/shared-memory/UDP, telemetry, GUI, TLS, and control in 50 s; `outputs/verification/20260905T184803Z-portable.log` |
| Sanitizer preset configure/build/CTest | **28 enabled tests passed**, 17.79 s; package intentionally disabled in this preset and passed normally; sanitizer coverage passed |
| Telemetry suite | **76/76 passed**, including QEMU state and raw observation integration |
| GUI suite/build | **13/13 passed**; Vite production build passed in 3.88 s with existing bundle warning |
| QEMU static/adversarial suite | Passed topology, raw schema/factory, PCAP, history, QMP status/loss/KVM denial, artifact tamper, CLI ordering, and Compose cases |
| Manifest verification | Passed against current source and artifacts |
| Compose render | External/default/history and container/default/history/KVM overlays passed after required inspection variables were supplied |
| QEMU runtime image | Built; `sha256:b318f8bad56b0310ead80cc5fd6eade631891122dc6b7c5ca762b27a6f516489`; user `65532:65532` |
| Base container security render | `privileged=false`; no added capability, device, Docker socket, host network, TUN, or NET_ADMIN; read-only and no-new-privileges enabled |
| External start on port 18090, explicit TCG | Passed; guest/application dual-protocol ready; counters 5 → 13 |
| External `status`, `verify`, `logs`, `token` | Passed; token value was not copied into this report |
| Real QMP pause/resume | CLI/API/GUI state transitions passed; monitor remained alive |
| Browser WebSocket check | Minimum sample count advanced 21 → 22 without refresh |
| Browser tab checks | Application, Network, History, Capture, History → Network, and return transitions rendered |
| Scoped control | unauthenticated pause 401; authenticated pause/resume 202; reset 200; only origin high-rate traffic paused |
| Real capture at final stop | Ethernet PCAPNG, 8,051 packets, 759 kB, 6,781 TCP and 1,258 UDP frames; SHA-256 `b6521fa6a9cea91dd480d1e4d25ca0d919b4e0cd98cfd3aa3e915765b56d86a7` |
| Real packet history | 1,258 rows, both TCP/UDP, 22-byte maximum preview in this run, 76-byte maximum captured/original lengths |
| Immediate restart with both opt-outs | Passed; capture disabled; packet-history endpoint returned 503 |
| Occupied loopback listener | Rejected before mutation with exit 1 and actionable address-in-use diagnostic |
| Stale PID containing unrelated live process | Unrelated process survived start and stop |
| Repeated stop / status after stop | Stop was idempotent; artifacts retained; status returned 1 without traceback; no orphan resources |
| Invalid accelerator / macOS KVM | Deterministic exits 64 / 2 before mutation |
| `scripts/check-format.sh` | Not run: exit 2, missing `clang-format-18` |
| `scripts/run-static-analysis.sh` | Not run: exit 2, missing `clang-tidy-18` |
| Fuzz preset | Configure passed; link failed because Apple libFuzzer runtime is unavailable; not counted as pass |

## 9. External-profile runtime results

The external profile is runtime verified on macOS/arm64 with the x86_64 guest
under explicit TCG. Actual acceleration was proven as TCG by QMP
`query-status + query-kvm`; KVM was reported absent/disabled. Supporting
services were separate Docker containers, while the inspected QEMU command was
an owned host process tied to the current capture path. QMP lived in a mode-0700
project state directory and was not TCP-published.

All four raw edge counters advanced. TCP and UDP were present in real Ethernet
capture. Packet history was a separate SQLite schema/API with required
direction, edge, protocol, address, port, length, truncation, and preview
fields. Capture and packet history were enabled by default and disableable.

QMP pause/resume proved the latest remediation at the evidence file, CLI, API,
and browser layers. Stop removed QMP/PID/container/network state while retaining
documented output. Immediate restart, repeated stop, occupied port, invalid
accelerator, and unrelated-PID cases behaved safely.

The external Linux `host-gateway` branch was not run and is not inferred from
the macOS result.

## 10. Container TCG and KVM results

No containerized guest was executed because the current host is macOS and the
profile correctly refuses non-Linux execution. Static and simulated evidence
is strong but remains inspection only:

- container/default, history, and KVM Compose overlays render;
- the runtime image builds as non-root UID/GID 65532;
- default Compose is read-only, capability-dropped, no-new-privileges, and has
  no device, Docker socket, TUN, host network, or privileged mode;
- the KVM overlay adds `/dev/kvm` and a caller-supplied discovered group;
- QMP probe, KVM denial, shutdown, healthcheck, and dual-protocol readiness
  paths have automated tests; and
- the container entrypoint passes the private QMP socket to both initial and
  continuous readiness monitors.

This does not prove a real Linux container guest boot, actual KVM enabled state,
relay/DNS recovery, signal propagation, live hardening, capture/history, GUI,
control, or cleanup.

## 11. Protocol, lifecycle, observation, and security assessment

- **Raw protocol:** ordinary bounded TCP/UDP with no GraphX magic, envelope, or
  `u32be` prefix on external edges.
- **Readiness:** QMP VM liveness and dual TCP/UDP guest readiness are separate,
  fresh, bounded, and loss-sensitive. Guest readiness cannot overwrite paused
  or unavailable QMP state.
- **Observation:** passive events use `kind: network_packet` and remain separate
  from GraphX message history. Attribution, unknown packets, malformed lengths,
  partial records, truncation, replacement, and parser recovery are bounded.
- **Capture:** source PCAP and Ethernet PCAPNG are bounded; active/incomplete,
  traversal, encoded traversal, symlink, hardlink, FIFO, oversize, malformed,
  and descriptor-race cases are tested.
- **History:** dedicated SQLite packet history has one-day/50,000-row/64-MiB
  defaults and bounded query, preview, migration, maintenance, failure, and
  restart behavior.
- **Control:** only `host-origin` is controllable. Observation works without a
  control token. Readiness probes continue while origin generation is paused,
  so passive counters can advance slowly; this is documented and does not
  imply guest control.
- **Exposure:** required host publications bind loopback; QMP is a Unix socket;
  internal container services bind only within the private Compose network.
- **Bounds:** capture 64 MiB/100,000 packets, history one day/50,000 rows/64 MiB,
  preview 64 bytes, source PCAP 64 MiB, and bounded QMP messages, waits, queues,
  retries, logs, and shutdown fallback.
- **Trust:** affected builds use the global certificate/install-script BuildKit
  secret mechanism without embedding organizational trust material.

## 12. Regression and compatibility assessment

Existing framed GraphX TCP/UDP, shared memory, TLS, capture runtime,
extcap/dissector, configuration CLI, version/release contract, package/external
consumer, Phase 11 UDP examples, telemetry security, history/control, and GUI
tests passed. The package test is disabled only in the sanitizer preset and
passed in fresh and portable normal suites.

No unavailable check was converted into a pass. Native Linux networking and
container runtime, exact LLVM 18 quality checks, and executable fuzzing remain
unverified in this environment.

## 13. Native Linux operator gate

Run from the same commit and dirty-tree snapshot on a native Linux x86_64 host.
Record distribution, kernel, CPU virtualization flags, Docker/Compose/QEMU,
compiler/CMake/Node/Python/TShark/SQLite, memory/storage, `/dev/kvm`
ownership/mode, commit, and `git status --short`.

### 13.1 Baseline and quality

```sh
cd ~/workspace/graphx-docker
scripts/verify.sh portable
scripts/test-linux-container.sh quality
scripts/test-linux-container.sh sanitizers
scripts/test-linux-container.sh fuzz
```

Configure `GRAPHX_CA_CERT` and/or `GRAPHX_CERT_INSTALL_SCRIPT` through
`scripts/configure-build-trust.sh` if organizational trust is required. Do not
copy certificates, private keys, or tokens into the operator report.

### 13.2 External Linux TCG

```sh
examples/qemu-node/external/scripts/demo.sh start --accel tcg
examples/qemu-node/external/scripts/demo.sh verify
examples/qemu-node/external/scripts/demo.sh status
examples/qemu-node/external/scripts/demo.sh logs
examples/qemu-node/external/scripts/demo.sh stop
examples/qemu-node/external/scripts/demo.sh stop
```

Require host-gateway routing, four flows, owned host QEMU/observer, capture,
history, GUI/control, immediate restart, and zero orphans.

### 13.3 Container TCG

```sh
examples/qemu-node/container/scripts/demo.sh start --accel tcg
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh status
examples/qemu-node/container/scripts/demo.sh logs
examples/qemu-node/container/scripts/demo.sh stop
examples/qemu-node/container/scripts/demo.sh stop
```

Require real dual-protocol guest readiness, QEMU inside `qemu-node`, all four
advancing edges, private data/QMP ports, capture/history defaults and opt-outs,
all GUI tabs, scoped controls, telemetry restart recovery, signals, forced
QEMU exit, immediate restart, and complete cleanup.

### 13.4 Container KVM, denial, and auto

```sh
examples/qemu-node/container/scripts/demo.sh start --accel kvm
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh status
```

Require retained QMP evidence with requested/selected/actual `kvm`,
`kvm.present=true`, and `kvm.enabled=true`. Inspect live user, groups, devices,
capabilities, mounts, publications, and security options. Then deny `/dev/kvm`
in a controlled test: explicit KVM must fail nonzero, while `auto` must either
select proven KVM or explicitly announce TCG fallback. A device mount alone is
not KVM proof.

### 13.5 Operator result matrix

| Gate | Required evidence | Result |
|---|---|---|
| Portable/native regression | log paths, counts, exact quality and fuzz results | **Pending operator** |
| External Linux TCG | host gateway, four flows, capture/history, cleanup | **Pending operator** |
| Container TCG | real guest, relay/DNS, complete CLI, signals/restart | **Pending operator** |
| Container KVM | QMP present+enabled and actual KVM | **Pending operator** |
| KVM denial/auto | deterministic failure and proven selection/fallback | **Pending operator** |
| Live container security | user, groups, caps, devices, mounts, ports | **Pending operator** |
| Lifecycle adversarial | pause/QMP loss, guest loss, signals, force, interruption | **Pending operator** |
| GUI/control parity | live nested states, tabs, WebSocket, scoped controls | **Pending operator** |
| Capture/history | defaults, opt-outs, bounds, restart, secure download | **Pending operator** |

## 14. Required remediation and readiness recommendation

There is no product-code remediation from this verifier turn. To unblock the
verdict:

1. Execute and record every row in Section 13 on native Linux.
2. Attach log paths, environment facts, QMP KVM proof, live security inspection,
   capture identities, and cleanup evidence without secrets.
3. Reconcile any Linux failure as an implementation finding rather than
   relabeling it as an environment skip.
4. Reissue this report as `ACCEPTED` only if QEMU-006 through QEMU-016 obtain
   their required Linux runtime/quality evidence and no P1/P2 defect appears.

Do not begin TAP/L2 guest networking, guest multicast/broadcast, physical SDR,
or arbitrary external-node orchestration until the Linux matrix is complete
and Phase 12 is accepted, unless the project owner explicitly accepts the
remaining runtime-evidence risk.
