# Phase 12 implementation handoff

## Post-Linux verification capture-inspection correction

A native-Linux operator run found that a confined TShark installation could
not open the retained `qemu-node.pcapng` through its workspace pathname. Phase
12 requires the operator to inspect that artifact, so the documented direct
`tshark -r PATH` command was not portable enough. The new bounded
`examples/qemu-node/scripts/inspect-capture.sh` helper verifies host readability,
opens the file as the operator, and streams it to TShark over standard input.
Demo verification now also fails if the retained PCAPNG is not host-readable,
and the QEMU static suite covers the stdin inspection contract.

## Post-verification Linux external-profile correction

A native-Linux operator run exposed a routing mismatch after the earlier phase
report: the observer correctly listened on the private `172.30.12.1` Compose
gateway, while `host.docker.internal` still resolved to Docker's default bridge
gateway. Telemetry therefore returned 503 for packet history, and the same
mapping could prevent the origin container from reaching QEMU's loopback-bound
TCP/UDP forwards.

The external launcher now pins `host.docker.internal` to `172.30.12.1` on Linux
and binds both QEMU forwards and the observer to that address. macOS keeps its
Docker Desktop mapping and loopback listeners. Startup verification now requires
all four edge counters to advance independently, reports history transport and
empty-history failures separately, and automatically rolls back owned runtime
resources on failure. Static Compose expansion covers the Linux mapping. A new
macOS/arm64 external-TCG lifecycle rerun passed with all four edge counters
advancing (`1 2 1 1 -> 4 5 2 2` at startup and `21 21 3 3 -> 24 24 4 4` on an
independent verify), capture/history available, and no owned runtime resources
left after stop. A new native-Linux external run remains required to convert the
Linux bridge correction from code inspection to runtime verification.

## Verification-round remediation (2026-09-05, live VM/guest state)

The current implementation addresses F-012-06, F-012-07, and F-012-08 from
`phase_12_verification.md`. The verification report remains the
pre-remediation record; the results below describe the updated working tree.

| Finding | Implemented remediation | Retest evidence | Current status |
|---|---|---|---|
| F-012-06 QMP state refresh | The readiness monitor performs bounded QMP `query-status` refreshes before TCP/UDP probe cycles, derives VM state without overwriting accelerator proof, degrades guest/protocol state while the VM is not running, stays alive during a pause, and re-probes after resume | Mock running/paused/recovery regression plus real external QMP `stop`/`cont`: VM `running -> paused -> running`, guest `ready -> unavailable -> ready`, TCP/UDP `true -> false -> true`; the same readiness PID remained alive | Implemented and externally runtime verified |
| F-012-07 GUI state projection | Telemetry projects independent boundary, VM, and guest-application states into `networkNodes`; both GUI topology mappings retain VM/guest/protocol fields; node cards render VM state, guest state, TCP/UDP readiness, and control capability | Telemetry and GUI model tests passed; the live browser retained the Network view after History, showed pause degradation, and recovered after resume | Implemented and externally runtime verified; repeat with Linux container profile |
| F-012-08 validation order | The Linux-only container-profile check now occurs before Docker, port, and artifact preflight | Non-Linux regression passes an invalid GUI port and still receives the deterministic Linux-only message and exit 2 | Implemented |

Final checks for this remediation round:

| Check | Result |
|---|---|
| Complete C++23 CTest | 28/28 passed in 15.36 s |
| `scripts/verify.sh portable` | Final updated-tree run passed C++23/C++20, topology, TCP/shared-memory/UDP, telemetry, GUI, TLS, and control in 51 s; log `outputs/verification/20260905T184145Z-portable.log` |
| Telemetry tests | 76/76 passed, including live-layer QEMU topology states |
| GUI tests and production build | 13/13 passed; build passed with the existing bundle-size warning |
| QEMU static/adversarial suite | Passed, including paused-monitor survival/recovery and non-Linux validation ordering |
| Live external QEMU/TCG | Dual-protocol readiness, API state transitions, GUI state transitions, History -> Network navigation, resume recovery, and clean stop passed on macOS/arm64 |
| Native Linux container TCG/KVM | Not run in this environment; remains the explicit operator gate below |

## Verification-round remediation (2026-09-05, readiness and CLI)

The latest `phase_12_verification.md` identified five additional findings.
This round addresses them without treating the native Linux runtime gate as
completed.

| Finding | Implemented remediation | Regression evidence |
|---|---|---|
| F-012-01 readiness | QMP records VM state but leaves overall state `booting`; a bounded TCP+UDP monitor alone promotes `ready`, refreshes protocol evidence, demotes failures, and telemetry demotes evidence older than ten seconds | QMP-only, TCP-only, dual-protocol, readiness-loss, and telemetry stale-evidence tests; external runtime retest |
| F-012-02 custom port/status | Effective GUI port is validated and saved in mode-0600 state; URL is rebuilt after load; status returns an actionable nonzero error for unreachable or invalid telemetry | Custom-port fresh-shell CLI runtime checks |
| F-012-03 immediate restart | TCP preflight enables safe address reuse so `TIME_WAIT` is not mistaken for a listener; real listeners remain rejected | Stop/immediate-start and occupied-listener checks |
| F-012-04 PID markers | External shutdown waits for owned helpers and removes QEMU, observer, readiness, and guard PID markers | Post-stop resource inspection |
| F-012-05 prompt drift | Reviewed Phase 12 contracts now live in repository `prompt/implement.md` and `prompt/verifier.md` | Direct comparison to the reviewed workspace pair |

QMP acceleration evidence, VM state, guest state, and overall state are now
separate. `vmState: running` cannot produce `guestState: ready` unless both
`guestProtocols.tcp` and `guestProtocols.udp` are true with fresh evidence.

Final remediation evidence:

| Check | Result |
|---|---|
| QEMU static/config tests | 4/4 passed; QMP-only, TCP-only, dual-protocol, loss, QMP timeout/KVM denial, capture/history, manifest, and Compose cases included |
| Telemetry tests | 76/76 passed, including booting, ready, and stale QEMU evidence |
| External macOS/TCG runtime | Custom port 18083 persisted across fresh-shell status/verify; evidence stayed fresh; VM freeze demoted to `degraded`; resume restored `ready` |
| Lifecycle regressions | Immediate stop/start passed; real listener was rejected; unreachable status returned nonzero without traceback; repeated stop left no PID/socket/container/network orphan |
| Portable acceptance | C++23 and C++20 28/28 plus all portable features passed in 54 s; log `outputs/verification/20260905T180736Z-portable.log` |
| Sanitizer acceptance | 28 enabled tests and sanitizer coverage passed in 18.08 s; package remains intentionally disabled only in this preset |

## Post-verification remediation (2026-09-05)

The implementation has been updated in response to all nine findings in
`phase_12_verification.md`. That verification report remains the immutable
record of the pre-remediation assessment; this handoff records the resulting
implementation and retest evidence.

| Finding | Implemented remediation | Retest evidence | Current status |
|---|---|---|---|
| F-001 accelerator proof | Added bounded QMP client; persists requested, selected, actual, `query-status`, and `query-kvm`; explicit KVM fails unless present and enabled | Mock QMP success, timeout, and KVM-denial tests; external TCG API reported QMP-proven TCG | Implemented; real KVM remains Linux gate |
| F-002 malformed PCAP | Enforces snaplen, captured/original relationship, original bound, nonzero capture, and timestamp fraction; bounded invalid records are skipped | Recovery tests cover zero, captured > original, captured > snaplen, uint32 original, bad timestamp, and a following valid packet | Implemented |
| F-003 history semantics | Migrates SQLite in place and exposes `capturedLength`, `originalLength`, and `truncated` while retaining `wireLength` compatibility | Migration and API/runtime history checks passed | Implemented |
| F-004 QMP shutdown | Uses `system_powerdown`, bounded wait, then QMP `quit`; process signal/KILL remains fallback | External start/verify/stop completed and private QMP socket was removed | Implemented externally; container signal gate remains Linux-only |
| F-005 artifact identity | Enables reproducible Buildroot mode and stable source epoch; creates/verifies kernel, rootfs, and guest-source manifest; retains and prints identities | Two consecutive builds produced identical three hashes; tamper and stale-source tests fail closed | Implemented |
| F-006 external placement | Rejects `lifecycle: external` nodes listed as managed deployment services | Native positive/negative configuration tests passed in C++20/C++23 | Implemented |
| F-007 GUI hierarchy/state | API and GUI distinguish host/container boundary, VM, and guest application; expose requested/selected/actual accelerators and six lifecycle states | Telemetry integration, GUI model tests, production build, and live external API inspection passed | Implemented; live container rendering remains Linux gate |
| F-008 QMP isolation | External QMP moved to mode-0700 state directory; container QMP moved to private tmpfs and is not mounted into observer/telemetry | Compose/image inspection and external cleanup check passed | Implemented |
| F-009 documentation | Common and profile guides now contain TCG/KVM steps, exact evidence fields, hierarchy/states, manifest, and capture/history semantics | Procedures were used for the external rerun; Linux section remains operator procedure | Implemented |

The Linux-only release gate is now narrowly scoped to runtime evidence: run the
container profile under TCG and KVM, confirm QMP KVM proof, exercise signal and
restart behavior, inspect live least privilege, exercise the GUI/control flow,
and confirm cleanup. It is no longer expected to compensate for missing product
instrumentation.

## Outcome

Phase 12 implements two profiles of one three-node, four-edge raw TCP/UDP QEMU
demonstration. The external profile was run on macOS/arm64 with an x86_64 guest
under TCG. The Linux container profile was schema-validated, image-built, and
security-inspected, but was not run because native Linux and `/dev/kvm` are not
available in this environment. Its TCG and KVM runtime acceptance is an
explicit manual operator gate.

The implementation does not represent ordinary guest packets as GraphX
envelopes. GraphX owns the topology, deployment description, observation,
bounded capture/history access, GUI, and host-origin control plane.

## Acceptance traceability

| ID | Implementation evidence | Validation evidence | Profile/platform | Status / remaining action |
|---|---|---|---|---|
| QEMU-001 | `external/graphx.yaml`, `container/graphx.yaml`; exact `origin-qemu-{tcp,udp}` and `qemu-receiver-{tcp,udp}` IDs | Config CTests and explicit topology-equivalence test in `scripts/test.sh` | Both / automated | Implemented |
| QEMU-002 | `data_plane: external`, `framing: none`; factory rejection in `src/transport_factory.cpp` | Positive/adversarial config tests; full CTest | Both / automated | Implemented |
| QEMU-003 | One guest tree plus reproducible `manifest.json` covering kernel, rootfs, and guest source | Two consecutive builds produced identical kernel/rootfs/manifest hashes; both launchers verify before mutation | Both / automated + external runtime | Implemented |
| QEMU-004 | External node is `runtime: qemu`, `execution: host`, `lifecycle: external`; QEMU and observer use owned PID files | API and browser showed host/external QEMU | External / macOS TCG | Implemented and runtime verified |
| QEMU-005 | Host QEMU launcher, private QMP, TCP/UDP readiness probe, bounded stop | Repeated start/verify/status/stop; counters advanced `32 -> 40`; repeated stop left no resources | External / macOS arm64, TCG | Runtime verified on macOS; external Linux host-gateway remains operator verification |
| QEMU-006 | Managed `qemu-node` service plus distinct container/VM/guest application API and GUI nodes | Config/API/GUI tests and Compose inspection | Container / inspection | Implemented; Linux runtime required |
| QEMU-007 | Runtime image, QMP proof, TCG, optional least-privilege KVM overlay | Mock proof/denial/timeout tests and image/security inspection pass | Container / image build only | Implemented; native Linux TCG/KVM runtime gate remains |
| QEMU-008 | Bounded readiness, owned processes, QMP powerdown/quit, signal fallback, health, rollback, idempotent stop | External lifecycle and graceful cleanup passed; container signal path inspected | External runtime; container inspection | Implemented externally; Linux signal/restart gate remains |
| QEMU-009 | PCAP tailer emits bounded `network_packet` telemetry; API/WebSocket uses raw edge counters | External counters advanced without refresh; telemetry integration test; browser showed live traffic | External runtime + automated | Implemented; repeat on Linux container |
| QEMU-010 | Source guard; bounded Ethernet PCAPNG; complete-block download validation; strict/recovering PCAP parser | Real capture plus malformed/recovery and incomplete-PCAPNG regressions pass | External + automated | Implemented; Linux runtime repetition required |
| QEMU-011 | Migrating SQLite history with original/captured lengths and truncation, age/count/size bounds, and bounded preview | Migration, API fields, pagination, restart, default, and disabled checks pass | External + automated | Implemented |
| QEMU-012 | Shared GUI with distinct deployment hierarchy, QMP-backed lifecycle, and requested/selected/actual acceleration | API/GUI tests and live external topology inspection passed | External runtime + automated | Implemented; live container rendering remains Linux gate |
| QEMU-013 | Only `host-origin` is controllable; guest control is `none`; generated in-memory token | Pause/resume sequence `57 -> 57 -> 63`; portable authenticated-control suite passed | External runtime + automated | Implemented; repeat on container profile |
| QEMU-014 | One `demo-profile.sh` behind thin profile wrappers; common verbs and options | External verbs exercised; unsupported macOS container start exited 2 with a clear Linux-only diagnostic | Both | Partial until all verbs run on Linux container |
| QEMU-015 | Loopback publications; QMP isolated in private state/tmpfs; bounded files, records, parser work, waits, logs, and previews | Security assertions, malformed input tests, complete PCAPNG validation, image builds, and port-conflict failure | Both / inspection + external runtime | Implemented; inspect live Linux KVM device/group |
| QEMU-016 | Additive config defaults preserve legacy graphs and framed transports | Portable C++20/C++23, package, UDP, telemetry 76/76, GUI 12/12, sanitizer 28/28 | Portable / macOS | Implemented for tested platform |
| QEMU-017 | Common guide, two profile READMEs, ADR, graphical-example guide, root README | Literal external procedure followed | Both | Implemented; Linux operator should annotate any host-specific corrections |

## Shared architecture and intentional differences

```text
host-origin (Docker) -- ordinary TCP + UDP --> x86_64 QEMU guest
x86_64 QEMU guest    -- ordinary TCP + UDP --> host-receiver (Docker)
                                 |
                                 +--> classic Ethernet PCAP
                                          |
                                          +--> shared packet observer
                                                |-- signed live network_packet events
                                                |-- bounded Ethernet PCAPNG
                                                `-- separate bounded SQLite history
```

The external profile runs QEMU and the observer on the host. This keeps the
observer beside the host-written active PCAP and avoids Docker Desktop
bind-cache latency. Telemetry and the GUI remain in Docker. On macOS the
observer history endpoint binds to loopback; on native Linux it binds only to
the private `172.30.12.1` demo bridge gateway.

The Linux profile runs QEMU, its TCP/UDP relay, and the same observer code in
containers. The KVM overlay adds only `/dev/kvm` and its discovered group; TCG
uses the default Compose model without a device.

## Plane separation

- Data plane: ordinary guest TCP/UDP, with no GraphX magic, envelope, or
  `u32be` prefix.
- Observation plane: validated Ethernet/IPv4/TCP/UDP metadata becomes the
  distinct `network_packet` event. Payload preview is capped at 64 bytes.
- Control plane: authenticated GraphX commands reach only `host-origin`.
  Pause/resume does not control or claim to pause QEMU. Reset clears collector
  counters and does not erase capture or packet history.

## Configuration and public interfaces

`NodeConfig` now supports bounded `runtime`, `execution`, `lifecycle`,
`control`, `accelerator`, and `architecture` metadata. `EdgeConfig` supports
`data_plane`. `framing: none` is valid only for explicitly external TCP/UDP
edges, and those edges are rejected by `TransportFactory`.

Both scripts provide:

```text
demo.sh start [--accel auto|kvm|tcg|hvf] [--no-capture] [--no-history]
demo.sh verify
demo.sh status
demo.sh logs
demo.sh token
demo.sh stop
```

External host publications are loopback-only: GUI/API TCP 8080, telemetry UDP
9000, QEMU forwarding TCP/UDP 18001, and receiver TCP/UDP 19001. The observer
history API uses TCP 9100 on loopback (macOS) or the private demo bridge
(Linux). QMP is a Unix socket in the private run directory. The container
profile publishes only the loopback GUI/API port.

Generated state is mode 0600 below `examples/qemu-node/.state` and ignored.
Run artifacts are retained below `outputs/qemu-node/<profile>/<timestamp>` and
ignored. Defaults are 64 MiB and 100,000 packets for capture, one day/50,000
records/64 MiB for packet history, two 1 MiB Docker log files per service, and
a 64 MiB source-PCAP ceiling that terminates QEMU rather than allowing
unbounded growth.

## Validation record

### Post-remediation results

| Command/check | Result |
|---|---|
| Development C++23 build and CTest | 28/28 passed in 16.14 s |
| `scripts/verify.sh portable` | Passed C++23/C++20, topology, TCP/shared-memory/UDP, telemetry, GUI, TLS, and control in 55 s; log `outputs/verification/20260905T171012Z-portable.log` |
| Telemetry | 76/76 passed, including PCAPNG completeness and QMP-backed QEMU topology |
| GUI | 12/12 passed; production build passed; existing large-bundle warning remains |
| ASan/UBSan | 28 enabled tests and sanitizer coverage passed in 18.38 s; package test is disabled only in this preset and passed normally |
| QEMU static/adversarial suite | Passed malformed-record recovery, history migration, QMP success/timeout/KVM denial, artifact tamper, topology equivalence, and Compose validation |
| Guest reproducibility | Two consecutive builds matched: kernel `5f5f46660c3d554913414295dea3df1521fafe78131437bc2f23cfc90ca50ced`; rootfs `b4379b452406cc2dceeadcd27c9fcd0bb9309b7a1ab2e858932a695d2f67f48d`; manifest `e655e73ce8e8ffdde7662e7fa1b773c7bd510dcd4c29c05ebc99d5cc6c6b7138` |
| External macOS/arm64 TCG | Start/verify/status/API/history/capture/QMP evidence/stop passed on port 18080; counters advanced; a forced QEMU exit changed API state to `degraded`; stopped evidence retained; private QMP and resources removed |
| Container image/model | Runtime image rebuilt with QMP/manifest tools as UID/GID 65532; Compose validated; execution correctly refused on non-Linux |
| Native Linux container TCG/KVM | Not run in this environment; operator gate remains |

Environment: macOS 26.6.2, Darwin 25.6.0 arm64; Docker 29.4.0; Compose 5.1.2;
QEMU 11.1.1; CMake 4.4.3; Apple Clang 21.0.0; Node 26.8.1; npm 11.19.0;
Python 3.13.5; TShark 4.6.8. Repository baseline was branch `main`, commit
`7cbb2ce`; pre-existing worktree was clean and Phase 12 changes remain
uncommitted.

| Command/check | Result |
|---|---|
| CMake Debug build + complete CTest | 28/28 passed, 15.26 s final targeted run |
| `scripts/verify.sh portable` | Passed C++23 and C++20, topology inspection, TCP/shared-memory/UDP, telemetry, GUI and control in 59 s; log `outputs/verification/20260905T152742Z-portable.log` |
| Telemetry `npm test` | 75/75 passed, 5.60 s |
| GUI `npm test` and production build | 11/11 passed; build passed in 3.75 s; existing 500 KiB chunk warning remains |
| Sanitizer preset | 28 enabled tests passed in 19.22 s; package test intentionally disabled by preset |
| External default profile | macOS/arm64, x86_64 guest, explicit TCG; start/verify/status/control/capture/history/stop passed |
| External opt-out profile | `--no-capture --no-history` passed; capture disabled and history returned 503 while live counters continued |
| Browser runtime | Four tabs worked in sequence; live topology, packet history, capture catalog, actual TCG and x86_64 displayed; no console errors |
| Real capture | `capinfos`: pcapng, Ethernet link type, 511 packets in final retained default run `20260905T152327Z`; `tshark` found both TCP and UDP |
| Container Compose/images | External/container Compose validation passed; service, telemetry, and QEMU runtime images built |
| Default container security | No privileged mode, Docker socket, added capability, device, TUN, or NET_ADMIN in base profile |
| Occupied GUI port | Failed before creating demo resources with an actionable nonzero diagnostic |
| Repeated stop | Passed and left no process/container/network orphan |
| Formatting/static analysis | Not run: required `clang-format-18` and `clang-tidy-18` are absent |
| Fuzz smoke | Not run to execution: targets compiled but Apple linker lacks `libclang_rt.fuzzer_osx.a`; environment limitation, not a pass |
| Linux container TCG/KVM | Not run by design; operator gate below |

## Linux operator gate

On a native Linux x86_64 host, follow `docs/qemu-demos.md` and run both:

```sh
examples/qemu-node/container/scripts/demo.sh start --accel tcg
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh status
examples/qemu-node/container/scripts/demo.sh stop

examples/qemu-node/container/scripts/demo.sh start --accel kvm
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh status
examples/qemu-node/container/scripts/demo.sh stop
```

Record `/dev/kvm` ownership/mode, live container device/group/capability
inspection, QMP or QEMU evidence that KVM actually activated, bidirectional
TCP and UDP, capture/history, all GUI tabs, pause/resume/reset, repeated stop,
and orphan checks. A device mount alone is not KVM proof.

## Known limitations and verifier focus

- Native Linux TCG, KVM, host-gateway behavior, device/group mapping, signal
  propagation, and cleanup remain unverified here.
- The observer supports partial records, malformed lengths/header rejection,
  truncation, and inode replacement. The independent verifier should stress
  repeated rotation, database byte pressure, corruption, and concurrent query
  load for longer durations.
- Source capture reaches a hard bound by stopping QEMU; transparent long-run
  QMP capture rotation is deferred.
- The GUI bundle-size warning is pre-existing quality debt; it is not a Phase
  12 correctness failure.
- Exact v18 format/static-analysis gates and fuzz execution must be run on a
  host with those toolchain components.
- TAP/L2 guest networking, guest multicast/broadcast, physical SDR integration,
  arbitrary external-node orchestration, and guest application control are
  explicitly deferred. Do not begin those extensions until Phase 12 Linux
  runtime acceptance is recorded or the project owner accepts the residual
  limitation.

## Key files

- `examples/qemu-node/README.md`
- `examples/qemu-node/external/graphx.yaml`
- `examples/qemu-node/external/compose.yaml`
- `examples/qemu-node/container/graphx.yaml`
- `examples/qemu-node/container/compose.yaml`
- `examples/qemu-node/scripts/demo-profile.sh`
- `examples/qemu-node/tools/packet_observer.py`
- `examples/qemu-node/tools/qmp_control.py`
- `examples/qemu-node/tools/artifact_manifest.py`
- `docs/qemu-demos.md`
- `docs/adr/0013-unified-qemu-profiles.md`
- `web/src/components/CapturePanel.jsx`
- `apps/telemetry/qemu.integration.test.mjs`
