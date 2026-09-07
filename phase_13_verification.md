# Phase 13 Independent Verification Report

**Verdict: CHANGES REQUIRED — portable Linux and native OVS/SPAN startup fail**

**Verification date:** 2026-09-06  
**Repository:** `/Users/rklinkhammer/workspace/graphx-docker`  
**Branch / baseline:** `main` / `c9db9bb9bfe26b122710eaa88a58a5506b50e378`  
**Verified state:** uncommitted Phase 13 working tree; no product implementation was changed by this verification  
**Contracts:** `prompt/implement.md`, `prompt/verifier.md`, and section 6 of `prompt/architectural_roadmap_implementation_plan.md`

**Linux follow-up:** Ubuntu 26.04.1 LTS, kernel 7.0.0-31-generic, x86_64,
commit `5cc44dd` on 2026-09-06. This follow-up independently executed the
portable simulated and native OVS/SPAN launchers. It supersedes the original
`INCOMPLETE` verdict; the original macOS evidence below remains valid for that
host and commit state.

## 1. Executive summary

Phase 13 is substantially implemented and its portable simulated-SDR profile
works on this macOS host. A real Docker run proved raw deterministic `SDR1` UDP
samples, raw TCP result delivery, TLS 1.3 mutual-authenticated control, live API
and WebSocket counters, GUI tab transitions, bounded Ethernet PCAPNG, separate
SQLite packet history, capture download/catalog behavior, and pause/resume relay
through the processor. The profile completed two start/verify/stop cycles, the
second with capture and history disabled. Existing portable compatibility,
quality, sanitizer, fuzz, telemetry, GUI, configuration, and documentation
checks passed.

Phase 13 is **not accepted**. The contract requires the portable simulation on
macOS and Linux and the OVS/SPAN profile on native Linux. This verifier only had
macOS with a Linux Docker Desktop VM; that environment cannot prove OVS,
macvlan, network namespaces, host listeners, SPAN traffic, privilege boundaries,
or exact native cleanup. Dry-run commands are inspection evidence only.

The Linux follow-up supplied both missing environments but neither mandatory
SDR runtime gate passed. The portable profile's `packet-capture` service cannot
create `/captures/sdr-node.pcap` in the mode-0700 bind-mounted run directory.
The native profile creates its OVS/macvlan/namespace infrastructure and starts
tcpdump, but writes `capture.pid` as root with mode 0600; the unprivileged
launcher therefore cannot validate the PID and rolls the run back. The standard
Linux regression suite still passed, including two 34/34 CTest runs and the
Docker feature suite.

Independent verification also found four implementation gaps:

1. native cleanup can delete fixed-name host resources without proving they
   belong to the current run;
2. the portable launcher has no failed-start/interruption rollback;
3. its occupied-port diagnostic raises `NameError`, and a prior state file
   silently overrides a newly supplied GUI port; and
4. the UDP processor accepts correctly formed traffic from any source rather
   than rejecting a mismatched endpoint.

The focused SDR protocol/security test is also narrower than the verifier
contract: it does not exercise minimum/maximum IQ counts, explicit count/length
corruption, loss/duplicate/reordering/restart behavior, invalid control framing
and JSON, untrusted/expired client certificates, or replay-independent
connections. These are evidence gaps requiring automated cases even where the
implementation appears sound by inspection.

No physical SDR was attached. Simulator success is not physical-hardware proof.

## 2. Evidence classification

- **Portable runtime — verified on macOS:** simulated profile, Docker bridge,
  API, WebSocket, GUI, control, capture, history, disable flags, and cleanup.
- **Portable automated:** C++20/C++23 suites, configuration/schema gates,
  telemetry, GUI, SDR protocol/TLS smoke checks, compatibility, packaging,
  quality, sanitizer, and fuzz gates.
- **Native-Linux runtime — not verified:** OVS, SPAN, macvlan, network namespace,
  native PID ownership, listeners, interruption recovery, and exact teardown.
- **Linux follow-up runtime — failed:** portable tcpdump output creation and
  native tcpdump PID ownership validation both block startup and acceptance.
- **Inspection only:** external configuration and Compose render, infrastructure
  dry-run, scripts, ownership declarations, capabilities, and documentation.
- **Not applicable:** physical SDR attachment and RF behavior.
- **Failed:** occupied-port diagnostics and the associated port-override case.

The implementation handoff was treated as a source of test leads, not as proof.

## 3. Environment and repository audit

| Item | Independently observed value |
|---|---|
| Host | macOS 26.6.2 (25G83), Darwin 25.6.0, arm64 |
| Docker | client/server 29.4.0; server Linux/arm64; Compose 5.1.2 |
| Native networking tools | `ovs-vsctl` absent; Linux `ip` absent; no native network namespaces |
| Packet tools | TShark 4.6.8; tcpdump 4.99.1 |
| Crypto/runtime | OpenSSL 3.6.3; Python 3.13.5 in shell |
| Build/web | CMake 4.4.3; Apple Clang 21.0.0; Homebrew Clang 21.1.8; Node 26.8.1; npm 11.19.0 |
| Privilege | passwordless `sudo` unavailable on this host |
| Repository | `main` at `c9db9bb`; dirty before verification with the Phase 13 implementation and prior documentation changes |
| Diff hygiene | `git diff --check` passed |

No repository `AGENTS.md` was present. Existing user changes were preserved.
Final portable cleanup left no Phase 13 containers or Docker network. Retained
runtime evidence remains under `outputs/sdr-node/simulated`.

## 4. Architecture and reuse assessment

Both configurations describe the same logical topology:

```text
SDR -- raw UDP IQ on 18400 --> processor -- raw TCP results on 18402 --> sink
SDR <-- TLS 1.3 mutual-authenticated TCP control on 18401 -------- processor
```

The three edges use `data_plane: external` and `framing: none`. The SDR is
`runtime: external` and `lifecycle: external` even when a Docker simulator
stands in for it. The processor is the sole visible controller. An accepted ADR
limits the cycle exception to descriptive external-plane edges; the GraphX
managed execution graph remains acyclic. `TransportFactory` rejects construction
of external raw edges.

Reuse is genuine. Both profiles use `examples/sdr-node/common` for the protocol,
simulator, processor, sink, controller, and generated demo TLS identities. They
also share the generalized `examples/qemu-node/tools/packet_observer.py`, the
telemetry service, GUI, history schema, capture conversion, command vocabulary,
and evidence formats. QEMU defaults remain when `GRAPHX_PACKET_RULES` is absent,
and the QEMU static regression passed.

The portable profile uses a private Docker bridge and is labeled as a
simulation. The external plan uses one macvlan network, `br-sdr`, three veth
ports, an all-traffic OVS mirror, a disposable namespace endpoint, and `sdr-cap`.
The plan never attaches a physical interface. These native properties were
inspected, not executed.

## 5. SDR-001 through SDR-012 traceability

| ID | Requirement | Implementation evidence/path | Independent validation evidence | Status | Remediation |
|---|---|---|---|---|---|
| SDR-001 | Equivalent one-SDR/switch/processor/sink raw topology and accurate ownership | `examples/sdr-node/{simulated,external}/graphx.yaml`; schema/config loader; ADR 0014 | Both configs validated and inspected; live simulated API/GUI showed 3 nodes, 3 raw edges, bridge simulation, and external SDR ownership | **Implemented** | Confirm the same normalized hierarchy in the native GUI run |
| SDR-002 | Deterministic bounded raw UDP IQ; malformed and mismatched input rejected; no GraphX framing | `common/protocol.py`, `sdr_simulator.py`, `processor.py`; UDP 18400 with `framing: none` | Deterministic bytes matched sender encoder and captured `SDR1`; live results reached sink; truncation and bad magic rejected | **Partial** | Bind acceptance to the expected source address/port or an explicit policy; add min/max, bad count/length, wrong endpoint, loss, duplicate, reorder, and restart tests |
| SDR-003 | TLS 1.3 mTLS, peer verification, four exact commands, bounded JSON lines, state/rejection tests | `common/sdr_simulator.py`, `processor.py`, `sdrctl.py`, `generate_tls.sh` | Live status/tune/stop/start passed; missing client cert, wrong server name, and range rejection passed; certs were seven-day and keys mode 0600 | **Partial** | Add malformed JSON/framing/multiple-record/action tests, untrusted/expired client certs, repeated independent connections, and strict per-action fields; reject `frequency_hz` on non-`tune` actions |
| SDR-004 | Shared implementation and consistent semantics; GUI reset remains collector-only | shared `common`, shared observer/telemetry/GUI, thin profile configs; GUI control target is processor | Both Compose files reference the shared code/image; QEMU static tests and GUI topology tests passed; pause/resume used processor mTLS; Reset documentation and existing GUI tests passed | **Implemented** | Reconfirm native profile uses identical command/evidence behavior |
| SDR-005 | Native Linux OVS bridge/ports/mirror, endpoint, packet path, and exact teardown | external graph, Compose, and `external/scripts/demo.sh`; infra planner | Linux created the veths, `br-sdr`, mirror, namespace, and macvlan network, then failed capture PID ownership validation and rolled back | **Failed** | Fix F-013-01 and F-013-07, then run the full native-Linux gate twice and inspect live frames, isolation, and teardown |
| SDR-006 | Packet-derived live API/WebSocket counters without fabricated GraphX messages | generalized packet observer emits `network_packet`; telemetry allow-list includes `ethernet-pcap`/`ovs-span` | Simulated counters advanced repeatedly; browser values advanced from 228/228 to 241/241 without refresh; traffic/history reported `network_packet` | **Partial** | Repeat API and WebSocket proof on portable Linux and native OVS/SPAN |
| SDR-007 | Default-on bounded Ethernet PCAPNG and separate history; query/download; disable flags | observer, telemetry capture/history APIs, Compose bounds, graph configs | macOS evidence passed; Linux default and opt-out starts both failed because tcpdump could not create its source PCAP | **Failed on Linux** | Fix F-013-06, then repeat default/opt-out/boundary checks on Linux and native SPAN |
| SDR-008 | Accurate GUI ownership; live updates, controls, capture/history, all tab transitions | existing web console plus SDR topology metadata/tests | Real browser showed topology/live changes; Application, Network, History, Capture, and History → Network transitions rendered; API control changed SDR state | **Partial** | Repeat browser checks on Linux/native topology; interactively enter a token and exercise buttons in an operator-controlled session |
| SDR-009 | Preflight, idempotence, waits, rollback, PID ownership, restart, cleanup | both `demo.sh` files; native PID marker checks and ERR trap | Linux confirmed portable failed-start leakage, stale-port traceback, unreadable native PID state, and non-idempotent native stop; native rollback did remove created host resources | **Failed** | Fix F-013-01 through F-013-03 and F-013-06/07; add fault injection for every startup stage, stale state, observer/capture/container failure, interruption, forged PID, and native repeat cleanup |
| SDR-010 | Private/loopback exposure, protected keys, least privilege, bounds, no broad kill or physical adoption | Compose hardening, loopback publish, private networks, TLS generation, bounded parsers/storage, fixed PID kill, ownership docs | Key/state modes inspected; telemetry only published to loopback; no broad `pkill`/`killall`; security/history/capture tests passed | **Partial** | Fix unowned native-resource deletion; inspect live native users/groups/caps/mounts/listeners/routes and capture limits |
| SDR-011 | No regressions to transports, QEMU, config, packaging, demos, or quality | additive schema/config changes and QEMU-compatible observer defaults | macOS portable suite passed; Linux full verification passed in 359 s with two 34/34 CTest runs and the Docker feature suite | **Implemented** | Retain both platform regression gates while remediating the SDR runtime failures |
| SDR-012 | Complete accurate architecture, ADR, indexes, profile guides, GUI/control/inspection/cleanup/troubleshooting | `examples/sdr-node` READMEs; `docs/GraphX_Architecture.md`; ADR 0014; examples/graphical/test guides | Documentation-consistency test passed and portable commands were followed literally; native commands inspected only | **Implemented** | Correct the documented/preflight port behavior after code remediation and append Linux results |

## 6. Wire, control, capture, history, and GUI results

### 6.1 Raw wire behavior

The default runtime capture was a standard Ethernet PCAPNG (link type 1).
Independent TShark filtering found UDP/18400, TCP/18401, and TCP/18402. The
first sample datagram began with hexadecimal `53445231` (`SDR1`), decoded to
sequence 0, 100 MHz, and 16 IQ pairs, and exactly matched
`encode_samples(0, 100000000, 16)`. It did not begin with GraphX `GXE` magic and
did not need a GraphX `u32be` length prefix. The largest valid encoded payload
by inspection is bounded at 1,042 bytes.

The processor validates magic, frequency, count, and exact payload length, then
sends a newline-delimited JSON result bounded to 4,096 bytes. The sink requires
exact fields and validates sequence, frequency, sample count, and power. The
live sink received results. However, `processor.py` records the UDP peer only for
logging and does not compare it with the configured SDR endpoint. This leaves
the verifier's wrong-endpoint/mismatched-packet requirement unsatisfied.

### 6.2 Control and TLS

The server and client both require TLS 1.3 minimum. The server requires a client
certificate signed by the demo CA; the client verifies the CA and the
`sdr-node` server name. Generated certificates were valid for seven days and all
private state files inspected were mode 0600. Live `status`, tune to 433.92 MHz,
`stop`, and `start` produced correct transitions. Missing-client-certificate,
wrong-server-name, and out-of-range tune checks passed.

Requests are newline-terminated and bounded at 4,096 bytes. The action set is
limited to `start`, `stop`, `tune`, and `status`, but the server accepts an
otherwise unused `frequency_hz` on `start`, `stop`, or `status`. The verifier
also requires negative certificate and framing cases not present in the focused
test suite. The boundary is therefore functional but not fully verified.

GUI Pause and Resume targeted only `processor`; the processor translated those
requests to SDR `stop` and `start` over mTLS. Reset retains the existing
collector-counter meaning. Observation did not require the browser control
token. The token itself was not copied into this report.

### 6.3 Capture and history

The default run produced `sdr-node.pcapng`, a bounded Ethernet capture, plus a
separate `packet_history` SQLite database. At one inspection point the database
held 1,586 rows: 775 `sdr-samples`, 36 `processor-control`, and 775
`processed-results`, with zero unknown records and a 64-byte maximum preview.
The capture catalog exposed one downloadable Ethernet file with 64 MiB and
100,000 packet defaults. The History page displayed the 50,000-record bound,
database size, bounded paging, and “return to newest” behavior.

With both opt-outs, the API reported capture `enabled:false`, no catalog files,
history `status:"disabled"`, no records, and zero captured packets. Empty
PCAPNG/SQLite scaffolding and the bounded classic PCAP observation source still
existed; no retained history rows or downloadable derived capture were exposed.
That behavior matches the documented observer architecture.

Shared QEMU observer tests cover PCAP replacement, partial/truncated records,
recovery, attribution fields, database bounds, and unknown traffic handling.
Telemetry tests cover capture path traversal, symlink/hardlink/FIFO rejection,
descriptor races, bounded catalogs/downloads, history retention/queue/storage
bounds, restart, and shutdown deadlines. A native SPAN run is still necessary
to prove those mechanisms on the Phase 13 host path.

### 6.4 Live GUI

A real browser at `http://127.0.0.1:18083/` rendered the SDR, processor, sink,
bridge-simulation path, and all three raw edges. Counts advanced without a page
refresh over WebSocket. Application, Network, History, and Capture tabs all
rendered, including History → Network and pagination transitions. The earlier
blank-tab symptom did not reproduce after allowing the asynchronous layout to
settle. Capture metadata and history bounds were visible.

Browser credential-transmission policy prevented the verifier from entering
the generated bearer token without operator confirmation. The same GUI control
endpoints and processor relay were exercised directly and passed, while web
unit tests verified in-memory token handling and SDR topology behavior. This is
recorded as a limited interactive check, not as a failed control implementation.

## 7. Findings

### F-013-01 — High: native cleanup can delete unowned fixed-name resources

`external/scripts/demo.sh` calls `cleanup` before collision preflight whenever
`start` runs. If a readable state file remains, cleanup unconditionally invokes
`graphx infra destroy` for the fixed configuration and deletes
`gx-sdr-device`; it validates only the two owned process commands. It does not
prove that `br-sdr`, `gx-sdr-native`, the veths, or the namespace were created by
the recorded run. A stale state file plus reused names can therefore delete an
unrelated bridge/network/namespace before `preflight_native_names` gets a chance
to reject the collision.

**Remediation:** persist a unique run/owner marker for every native resource,
label Docker/OVS resources where supported, validate exact topology and owner
before deletion, and refuse ambiguous cleanup with an actionable diagnostic.
Run stale-state, forged-state, name-collision, and unrelated-resource survival
tests at every startup stage.

### F-013-02 — High: portable failed starts do not roll back

The simulated launcher's `start` path creates state, certificates, a run
directory, and Compose resources, then calls `verify`, but it has no
`ERR`/`INT`/`TERM` cleanup trap. A failed image/service, observer/capture crash,
readiness timeout, failed verification, or interrupted start can leave the
project running. The external launcher has a trap, but portable lifecycle is
also explicitly required to roll back failed starts.

**Remediation:** install the rollback trap before the first mutation, make it
idempotent, remove only the current Compose project/resources, retain bounded
diagnostic evidence, and add injected build/service/readiness/observer/capture
failure and interruption tests.

### F-013-03 — Medium: occupied-port handling crashes and restart ignores a new port

With port 18083 deliberately occupied, `GRAPHX_SDR_GUI_PORT=29000 demo.sh start`
still checked 18083 because `load_state` overwrote the new environment value.
After 50 retries, Python raised `NameError: name 'error' is not defined` because
the exception target is cleared after the `except` block. The command exited 1,
but not with the promised actionable port diagnostic. A naturally transient
reuse immediately after stop exposed the same traceback once.

**Remediation:** preserve explicit invocation-time overrides before loading old
state, store the last exception outside the `except ... as` target, identify the
actual checked port, and test occupied, invalid, changed, and immediately reused
ports for both profiles.

### F-013-04 — Medium: UDP source endpoint is not enforced

`processor.py` accepts any datagram reaching UDP/18400 if the `SDR1` contents
are structurally valid. It does not validate the peer against the configured SDR
address/port, and the focused test only checks observer attribution with a wrong
protocol. This conflicts with SDR-002's mismatched-packet rejection and the
verifier's wrong-endpoint case.

**Remediation:** define the intended endpoint policy (exact address and, if the
sender uses a stable port, source port), validate it before decoding/processing,
count rejected packets without leaking payloads, and add wrong-address/port,
duplicate, reorder, loss, and restart tests.

### F-013-05 — Medium: focused control/protocol adversarial coverage is incomplete

The implementation has useful parser and mTLS safeguards, but
`tests/test_sdr_example.py` covers only one ordinary sample count, empty/
truncated/bad-magic payloads, a client without a certificate, wrong server name,
one bad tune range, and the positive command sequence. It does not meet the
full verifier matrix. Direct inspection also showed that unused
`frequency_hz` is accepted on non-tune actions.

**Remediation:** add table-driven min/max and corrupt header/count/length tests;
bounded line tests for missing newline, oversize, trailing/multiple records, bad
UTF-8/JSON/type/field/action/range; untrusted and expired client identities;
fresh-connection/replay independence; and exact per-action schemas.

### F-013-06 — High: portable packet capture cannot start on Linux

On native Linux, both the default simulated launch and
`start --no-capture --no-history` fail verification because the
`packet-capture` container exits with:

```text
tcpdump: /captures/sdr-node.pcap: Permission denied
```

The launcher creates the host run directory mode 0700 as the invoking user.
Compose starts the capture service as `0:0`, but Linux bind-mount permissions
and tcpdump's privilege behavior leave it unable to create the output file.
The opt-out flags disable derived capture/history behavior, not this source
capture service, and `verify` still requires the service. In both cases `start`
returned 1 while the other five services remained running, independently
confirming F-013-02. Manual `stop` was required and did remove them.

**Remediation:** give the capture process explicit, least-privilege write access
to a dedicated capture path without broadening unrelated state permissions;
define whether source capture is required in opt-out mode; make failed start
rollback automatic; and add a native-Linux bind-mount permission regression.

### F-013-07 — High: native capture PID state is unreadable by its owner check

The native Linux profile successfully created the OVS bridge, mirror ports,
namespace, and `gx-sdr-native`, and tcpdump reported that it was listening on
`sdr-cap`. Startup nevertheless reported `tcpdump failed to start on the OVS
mirror port`. The privileged shell wrote `capture.pid` as `root:root`, mode
0600, in the mode-0700 evidence directory. The unprivileged `owned_pid` check
cannot read that file, so it can never establish ownership and triggers
rollback. Teardown then removes the interface, producing tcpdump's
`pcap_loop: The interface disappeared` diagnostic.

The rollback removed all observed fixed interfaces, OVS bridge, namespace,
Docker network, processes, and containers. However, the documented subsequent
`stop` and a repeated `stop` both returned 1, so cleanup is not idempotent at
the command level even when resources are absent.

**Remediation:** create PID metadata with ownership and mode readable by the
launcher while retaining strict write ownership, validate it before declaring
startup failure, and make cleanup return success when owned resources are
already absent. Add startup, rollback, and repeated-stop tests on native Linux.

### Advisory — production web bundle size

The production build passed but Vite reported a pre-existing 1.86 MB JavaScript
chunk (approximately 571 kB gzip), above its 500 kB warning threshold. This is
not a Phase 13 acceptance blocker, but later route/component code splitting
would improve initial GUI load.

## 8. Lifecycle and security assessment

Portable start/status/verify/logs/token/control/stop workflows were exercised.
Logs streamed correctly and were intentionally interrupted by the verifier.
Invalid start/control options exited 64 with usage. External start on macOS
exited 2 before native mutation with the correct native-Linux diagnostic. Two
portable stops were safe and left no containers/network; evidence remained.

The portable GUI is published only on `127.0.0.1`, application traffic stays on
the private bridge, the telemetry service runs non-root, and the source/capture
services receive only the narrowly required packet-capture capability. Secrets
are generated locally, are not embedded in the repository, and were mode 0600.
All waits, datagrams, line requests, previews, history rows/bytes/time, capture
bytes/packets/catalogs, and telemetry queues inspected have explicit bounds.
The documentation accurately states that SDR UDP is unauthenticated,
unreliable, and unencrypted.

The native script uses PID files plus command markers rather than broad process
matching and contains no `pkill` or `killall`. That process ownership check is a
good boundary but does not solve F-013-01 for host networking resources. Native
least privilege, listeners, routes, limits, and cleanup remain unproven.

## 9. Commands and results

| Command/check | Evidence class | Result |
|---|---|---|
| `scripts/verify.sh quick` | Portable build/test | **PASS** — fresh configure/build; 34/34 CTests; log `outputs/verification/20260906T231147Z-quick.log` |
| `scripts/verify.sh quality` | Static quality | **PASS** — format on 45 C++ files, clang-tidy, cppcheck; log `outputs/verification/20260906T231213Z-quality.log` |
| `scripts/verify.sh sanitizers` | Portable automated | **PASS** — 34 tests under the supported macOS UBSan profile; log `outputs/verification/20260906T231238Z-sanitizers.log` |
| `GRAPHX_FUZZ_SECONDS=5 scripts/verify.sh fuzz` | Portable automated | **PASS** — both fuzzers, five seconds each; log `outputs/verification/20260906T231306Z-fuzz.log` |
| `scripts/verify.sh portable` | Cumulative portable acceptance | **PASS** — C++23 and C++20 34/34, all topology checks, pipelines, telemetry 76/76, GUI 14/14, web build, TLS/control; 58 s; log `outputs/verification/20260906T232012Z-portable.log` |
| `python3 tests/test_sdr_example.py .` | Automated simulated dependency | **PASS** for its implemented protocol/observer/TLS cases; coverage gaps listed in F-013-04/05 |
| both `graphx validate` and `graphx inspect` | Configuration/model | **PASS** — simulated and external each report three nodes/three external raw edges and intended network/path metadata |
| both `docker compose ... config --quiet` | Compose inspection | **PASS** after supplying the generated state variables |
| `bash -n` on all Phase 13 shell entry points | Static syntax | **PASS** |
| `graphx infra create/destroy/status external/graphx.yaml --dry-run` | Inspection only | **PASS** as plan rendering; no native resources were created |
| simulated default `start/verify/status/token/control/logs/stop` | macOS Docker runtime | **PASS** — six services, counters, TLS control, result sink, capture/history, and cleanup |
| `capinfos`, TShark, payload decode, SQLite query | macOS runtime | **PASS** — Ethernet PCAPNG, all three ports/edges, exact `SDR1` bytes, separate bounded history |
| real browser GUI/API/WebSocket | macOS runtime | **PASS with noted interactive-token limitation** — live update and all tab transitions rendered |
| second simulated start with `--no-capture --no-history`; repeated stop | macOS Docker runtime | **PASS** — features reported disabled and cleanup was idempotent |
| occupied 18083 plus requested override 29000 | Adversarial runtime | **FAIL** — old state overrode the request and diagnostic ended in Python `NameError` |
| invalid CLI/control options | Adversarial runtime | **PASS** — exit 64, bounded usage output |
| external start on macOS | Platform rejection | **PASS** — exit 2 before native mutation |
| native external start/verify/stop twice | Native-Linux runtime | **NOT RUN / blocked by host** |
| physical SDR | Hardware | **NOT APPLICABLE** |

### 9.1 Linux follow-up commands and results

| Command/check | Evidence class | Result |
|---|---|---|
| `scripts/verify.sh full` | Linux cumulative regression | **PASS** — 359 s; C++20/C++23 each 34/34; Docker feature suite passed; log `outputs/verification/20260906T235418Z-full.log` |
| focused Phase 13 CTests | Linux automated | **PASS** — documentation, QEMU static regression, SDR behavioral test, and both SDR configuration tests, 5/5 |
| `python3 tests/test_sdr_example.py .` | Linux automated SDR | **PASS** for implemented cases; F-013-04/05 gaps remain |
| simulated default `start` | Linux Docker runtime | **FAIL** — packet capture permission denied; exit 1; five services leaked until manual stop |
| simulated `start --no-capture --no-history` | Linux Docker runtime | **FAIL** — capture service still started and required; same leak and manual cleanup |
| occupied 8080 plus requested override 29000 | Linux adversarial runtime | **FAIL** — stale state selected 8080 and preflight raised `NameError` |
| external native `start` | Linux OVS/SPAN runtime | **FAIL** — OVS/network setup succeeded; unreadable root-owned `capture.pid` caused false startup failure and rollback |
| native rollback inspection | Linux lifecycle | **PASS with limitation** — all observed fixed resources/processes were absent; ownership-safety finding F-013-01 remains |
| native `stop` after rollback and repeated `stop` | Linux lifecycle | **FAIL** — both returned 1 rather than succeeding idempotently |

One initial manual Compose render omitted exported state variables and one
inspection command incorrectly attempted to parse human-readable output as
JSON. Both were verifier setup errors; corrected repetitions passed and neither
is attributed to the product.

## 10. Native-Linux re-verification still required

After remediating F-013-01 through F-013-07, rerun on a native Linux host with
Docker, Compose, Open vSwitch, iproute2, tcpdump/TShark, and suitable sudo:

```bash
scripts/verify.sh portable
examples/sdr-node/simulated/scripts/demo.sh start
examples/sdr-node/simulated/scripts/demo.sh verify
examples/sdr-node/simulated/scripts/demo.sh stop

examples/sdr-node/external/scripts/demo.sh start
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh verify
sudo ovs-vsctl show
sudo ovs-vsctl list Mirror
sudo ip netns exec gx-sdr-device ip -details address show
examples/sdr-node/external/scripts/demo.sh stop
```

Repeat both profiles twice and execute the complete adversarial list from
`prompt/verifier.md`: interrupt every native startup stage; inject occupied
ports/names, missing GraphX/OVS/ip/tcpdump, invalid credentials, failed
containers/observer/capture, stale and forged PID/state, and capture limits.
Independently inspect OVS port membership/mirror output, macvlan parent,
namespace address/MAC/routes/listeners, packet path and SPAN frames, container
isolation, GUI hierarchy/live changes, retained evidence, and exact teardown.

After stop, prove the absence of `br-sdr`, `gx-sdr-device`, `gx-sdr-native`, all
fixed `sdr*` veths, listeners, and owned processes while demonstrating that
unrelated collision fixtures survive. Append command output, host versions,
retained evidence paths, and the Linux matrix result to this report.

## 11. Architectural drift, documentation, and risk summary

- The implementation handoff overstates SDR-002, SDR-003, and portable SDR-009
  as implemented; the independent cases above show they are partial.
- The handoff says the port preflight was fixed, but the occupied-port path is
  still broken and restart-time overrides are not honored.
- Documentation is otherwise internally consistent and accurately distinguishes
  the portable bridge, native OVS/SPAN, and optional physical attachment.
- The external topology and dry-run are not runtime OVS evidence.
- UDP remains deliberately unauthenticated/unencrypted/unreliable; endpoint
  filtering reduces accidental/spoofed acceptance but does not authenticate a
  physical SDR.
- Native network cleanup is the highest operational risk and must be corrected
  before asking an operator to run fault-injection tests with sudo.
- No stale Phase 13 acceptance claim was found: `verification_status.md` already
  excluded Phase 13 from the accepted chain pending independent verification.

## 12. Exit decision and prioritized remediation

**Overall result: CHANGES REQUIRED.** Phase 13 cannot enter the accepted chain.

Immediate blockers:

1. make native cleanup ownership-safe (F-013-01);
2. add portable failed-start rollback (F-013-02);
3. correct port precedence and occupied-port diagnostics (F-013-03); and
4. fix Linux portable capture access and native PID ownership/idempotence
   (F-013-06/07); and
5. enforce/document the UDP endpoint policy and add missing adversarial wire and
   control tests (F-013-04/05).

Next verification work:

1. rerun the macOS portable lifecycle/adversarial cases after remediation;
2. run the portable simulated profile and regressions on Linux; and
3. run the complete native-Linux OVS/SPAN, interruption, ownership, security,
   capture/history, GUI, repeat-start, and exact-cleanup matrix.

Later enhancement:

- split the large GUI production bundle and retain this as non-blocking quality
  work.

Phase 13 may be marked `ACCEPTED` only after the findings are closed with direct
evidence and both mandatory Linux gates pass. A physical SDR remains optional.

## Appendix A — retained evidence

- Default simulated run:
  `outputs/sdr-node/simulated/20260906T231404Z`
- Capture/history-disabled run:
  `outputs/sdr-node/simulated/20260906T231911Z`
- Verification logs:
  `outputs/verification/20260906T231147Z-quick.log`,
  `outputs/verification/20260906T231213Z-quality.log`,
  `outputs/verification/20260906T231238Z-sanitizers.log`,
  `outputs/verification/20260906T231306Z-fuzz.log`, and
  `outputs/verification/20260906T232012Z-portable.log`

## Appendix B — independent evidence boundaries

The macOS Docker engine reports Linux/arm64, but it runs inside Docker Desktop.
It is valid evidence for the portable container profile only. It is not a
native-Linux host and provides no evidence for host OVS, SPAN, macvlan parent,
network namespace, `/proc` process ownership, Linux capabilities, physical NIC
safety, or exact host teardown. Likewise, generated namespace traffic on Linux
will be evidence for the disposable simulator and network path, not for a real
SDR's firmware, timing, RF behavior, or production certificate lifecycle.
