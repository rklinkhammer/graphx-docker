# Phase 13 Independent Verification Report

**Verdict: ACCEPTED — Linux-host remediation and mandatory runtime gates pass**

**Verification date:** 2026-09-06  
**Repository:** `/Users/rklinkhammer/workspace/graphx-docker`  
**Original branch / baseline:** `main` / `c9db9bb9bfe26b122710eaa88a58a5506b50e378`
**Current revalidation:** `main` / `9d6023802e169e27116ba9019123712fa0bfccab` on 2026-09-07
**Verified state:** the updated, checked-in Phase 13 implementation; no product implementation was changed by this verification
**Contracts:** `prompt/implement.md`, `prompt/verifier.md`, and section 6 of `prompt/architectural_roadmap_implementation_plan.md`

**Linux follow-up:** Ubuntu 26.04.1 LTS, kernel 7.0.0-31-generic, x86_64,
commit `5cc44dd` on 2026-09-06. This follow-up independently executed the
portable simulated and native OVS/SPAN launchers. It supersedes the original
`INCOMPLETE` verdict; the original macOS evidence below remains valid for that
host and commit state.

**Remediation revalidation:** commit `6d1b36f` on the same Ubuntu host on
2026-09-07. The ownership guard, port handling, portable rollback, UDP source
policy, and native PID-file handling improved. The current focused SDR CTest,
portable Linux runtime, and native OVS/SPAN runtime still failed, so Phase 13
remained outside the accepted chain at that checkpoint.

**Linux-host implementation completion:** the working tree based on `6d1b36f`
was independently rerun on 2026-09-07 after the focused fixes below. Both
portable simulated cycles and both native OVS/SPAN cycles passed, including
default capture/history, opt-out APIs, live counters, TLS control, retained
evidence, rollback, repeated stop, and exact resource cleanup. The full Linux
verification profile passed in 340 seconds. This evidence supersedes the
`CHANGES REQUIRED` remediation-revalidation result above.

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

**Superseded macOS-only verdict:** Phase 13 was not accepted at the original
macOS-only checkpoint. The contract requires the portable simulation on
macOS and Linux and the OVS/SPAN profile on native Linux. This verifier only had
macOS with a Linux Docker Desktop VM; that environment cannot prove OVS,
macvlan, network namespaces, host listeners, SPAN traffic, privilege boundaries,
or exact native cleanup. Dry-run commands are inspection evidence only.

**Superseded initial Linux follow-up:** the first Linux follow-up supplied both missing environments but neither mandatory
SDR runtime gate passed. The portable profile's `packet-capture` service cannot
create `/captures/sdr-node.pcap` in the mode-0700 bind-mounted run directory.
The native profile creates its OVS/macvlan/namespace infrastructure and starts
tcpdump, but writes `capture.pid` as root with mode 0600; the unprivileged
launcher therefore cannot validate the PID and rolls the run back. The standard
Linux regression suite still passed, including two 34/34 CTest runs and the
Docker feature suite.

At `6d1b36f`, portable failed-start rollback and repeated stop pass, explicit
port overrides and diagnostics pass, and a wrong-owner Docker network survives
native cleanup refusal. However, portable tcpdump still receives `Permission
denied` on its bind-mounted output. Native tcpdump captures packets, but creates
the source PCAP as `root:root` mode 0600, so the non-root observer records no
history and counters do not advance. The expanded SDR test also fails its
positive host-Python mTLS path with `SSLV3_ALERT_UNSUPPORTED_CERTIFICATE`.

The Linux-host implementation closes those remaining failures. Portable
tcpdump retains a capability-bounded root identity with `NET_RAW`, `NET_ADMIN`,
and `DAC_OVERRIDE`, which permits only the mounted run directory to bypass host
DAC while the container root filesystem remains read-only. Native tcpdump opens
the mirror with sudo and drops to the invoking operator, keeping initial and
rotated PCAP files readable by the non-root observer. The focused TLS test now
waits for explicit server-context readiness before mutating process-wide test
credentials.

The original independent verification also found four implementation gaps,
all of which are retained below as superseded history:

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
- **Native-Linux runtime — verified by checked-in Linux-host evidence:** two
  native OVS/SPAN cycles covered the bridge, ports, mirror, namespace endpoint,
  source capture ownership, live observation, control, opt-outs, and exact
  teardown. The macOS revalidation did not re-execute Linux host networking.
- **Linux follow-up runtime — historical failure, superseded:** portable tcpdump
  output creation and native tcpdump PID ownership initially blocked startup;
  later Linux completion cycles passed after remediation.
- **Inspection only:** external configuration and Compose render, infrastructure
  dry-run, scripts, ownership declarations, capabilities, and documentation.
- **Not applicable:** physical SDR attachment and RF behavior.
- **Adversarial portable runtime — verified:** occupied and invalid GUI ports
  fail with actionable nonzero exits and leave no Compose resources.

The implementation handoff was treated as a source of test leads, not as proof.

## 3. Environment and repository audit

This table records the original macOS checkpoint. The current repository and
tool revalidation is recorded in section 9.4.

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
The plan never attaches a physical interface. At the original macOS checkpoint,
these native properties were inspected but not executed; the later Linux-host
completion supplied the native runtime evidence.

## 5. SDR-001 through SDR-012 traceability

| ID | Requirement | Implementation evidence/path | Independent validation evidence | Status | Remediation |
|---|---|---|---|---|---|
| SDR-001 | Equivalent one-SDR/switch/processor/sink raw topology and accurate ownership | `examples/sdr-node/{simulated,external}/graphx.yaml`; schema/config loader; ADR 0014 | Both configs validated and inspected; live simulated API/GUI showed 3 nodes and 3 raw edges; Linux native verification confirmed the external hierarchy and ownership boundary | **Implemented** | Retain cross-profile topology checks |
| SDR-002 | Deterministic bounded raw UDP IQ; malformed and mismatched input rejected; no GraphX framing | `common/protocol.py`, `sdr_simulator.py`, `processor.py`; UDP 18400 with `framing: none` | Remediation adds source-address enforcement and boundary, corruption, duplicate, reorder, loss, and restart cases; these execute before the later TLS test failure | **Implemented** | Retain the new source-policy and protocol cases |
| SDR-003 | TLS 1.3 mTLS, peer verification, four exact commands, bounded JSON lines, state/rejection tests | `common/sdr_simulator.py`, `processor.py`, `sdrctl.py`, `generate_tls.sh` | Expanded host test and live portable/native controls pass; malformed framing, certificate, replay, schema, and command cases complete | **Implemented** | Retain cross-platform host-Python and live mTLS coverage |
| SDR-004 | Shared implementation and consistent semantics; GUI reset remains collector-only | shared `common`, shared observer/telemetry/GUI, thin profile configs; GUI control target is processor | Both Compose files reference the shared code/image; QEMU static tests and GUI topology tests passed; pause/resume used processor mTLS; Reset documentation and existing GUI tests passed | **Implemented** | Retain shared-semantics regression coverage |
| SDR-005 | Native Linux OVS bridge/ports/mirror, endpoint, packet path, and exact teardown | external graph, Compose, and `external/scripts/demo.sh`; infra planner | Two Linux native cycles passed; OVS/SPAN counters advanced, history was queryable, controls worked, PCAP stayed operator-owned, and teardown was exact | **Implemented** | Retain repeated native ownership and cleanup checks |
| SDR-006 | Packet-derived live API/WebSocket counters without fabricated GraphX messages | generalized packet observer emits `network_packet`; telemetry allow-list includes `ethernet-pcap`/`ovs-span` | Simulated browser counters advanced without refresh; Linux portable and native OVS/SPAN counters advanced; traffic/history reported `network_packet` | **Implemented** | Retain live portable and native counter checks |
| SDR-007 | Default-on bounded Ethernet PCAPNG and separate history; query/download; disable flags | observer, telemetry capture/history APIs, Compose bounds, graph configs | Portable and native default cycles produced live counters/history/capture; both opt-out cycles reported disabled APIs and zero retained records | **Implemented** | Retain Linux bind-mount and native rotation ownership checks |
| SDR-008 | Accurate GUI ownership; live updates, controls, capture/history, all tab transitions | existing web console plus SDR topology metadata/tests | Real browser showed topology/live changes; Application, Network, History, Capture, and History → Network transitions rendered; direct and GUI-relayed controls changed SDR state; Linux runtime confirmed native observation | **Implemented** | Retain browser tab-transition and control checks |
| SDR-009 | Preflight, idempotence, waits, rollback, PID ownership, restart, cleanup | both `demo.sh` files; native PID marker checks and ERR trap | Port diagnostics, rollback, readable PID files, per-run ownership refusal, two cycles per profile, repeated stop, and exact cleanup passed | **Implemented** | Retain lifecycle fault-injection coverage |
| SDR-010 | Private/loopback exposure, protected keys, least privilege, bounds, no broad kill or physical adoption | Compose hardening, loopback publish, private networks, TLS generation, bounded parsers/storage, fixed PID kill, ownership docs | Key/state modes and capabilities were inspected; telemetry publishes only to loopback; no broad `pkill`/`killall`; Linux ownership refusal, source-PCAP ownership, security/history/capture, and exact-cleanup checks passed | **Implemented** | Retain collision-fixture, capability, and cleanup audits |
| SDR-011 | No regressions to transports, QEMU, config, packaging, demos, or quality | additive schema/config changes and QEMU-compatible observer defaults | Current full Linux verification passed in 340 s with three 34/34 CTest runs and the Docker feature suite | **Implemented** | Retain both platform regression gates |
| SDR-012 | Complete accurate architecture, ADR, indexes, profile guides, GUI/control/inspection/cleanup/troubleshooting | `examples/sdr-node` READMEs; `docs/GraphX_Architecture.md`; ADR 0014; examples/graphical/test guides | Documentation-consistency test passed; portable commands were followed literally; checked-in Linux results cover the native workflow | **Implemented** | Retain documentation-command consistency checks |

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

**Revalidation at `6d1b36f`: PARTIAL REMEDIATION.** Per-run Docker, OVS, link,
and namespace markers are now present. A disposable `gx-sdr-native` network
with the wrong owner label survived `stop`, which refused cleanup with exit 2.
The complete collision and forged-state matrix has not yet run.

**Linux-host implementation completion: CLOSED.** The checked-in Linux result
records per-run ownership refusal, two successful native cycles, repeated stop,
and exact cleanup without removing the wrong-owner collision fixture. Retain
the broader forged-state and startup-interruption matrix as regression work,
not as an open Phase 13 acceptance blocker.

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

**Revalidation at `6d1b36f`: CLOSED FOR OBSERVED FAILURE.** Both default and
opt-out capture failures triggered rollback and left zero Compose containers or
networks. Repeated stop returned success. Interruption injection remains part
of the broader lifecycle matrix.

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

**Revalidation at `6d1b36f`: CLOSED.** A free explicit port overrode occupied
stale state, invalid input produced the intended integer diagnostic, and an
occupied port produced the intended address-in-use diagnostic without
`NameError`.

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

**Revalidation at `6d1b36f`: CLOSED FOR THE DOCUMENTED ADDRESS POLICY.** Both
profiles configure `SDR_SAMPLE_SOURCE`; the processor rejects a mismatched IPv4
source before decoding, and focused assertions cover source and sequence
behavior.

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

**Revalidation at `6d1b36f`: PARTIAL REMEDIATION.** The requested protocol,
framing, schema, untrusted/expired certificate, and fresh-connection cases were
added, but the test fails before completing them; see F-013-09.

**Linux-host implementation completion: CLOSED.** The readiness remediation
allowed the expanded table of protocol, schema, framing, certificate, and
fresh-connection cases to complete on Linux. The same focused test also passes
in the current macOS revalidation.

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

**Revalidation at `6d1b36f`: OPEN.** Changing the run directory to mode 0770,
retaining root, adding `SETUID`/`SETGID`, and using `tcpdump -Z root` did not
resolve this daemon's bind-mount mapping. Default and opt-out starts still log
the same permission denial. Rollback now succeeds.

**Linux-host implementation: CLOSED.** The capture service now adds only
`NET_RAW`, `NET_ADMIN`, and `DAC_OVERRIDE` to its otherwise dropped capability
set. Default and opt-out portable cycles both passed, and generated evidence is
readable by the host UID and observer.

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

**Revalidation at `6d1b36f`: CLOSED.** Both PID files were readable by the
launcher, startup advanced through OVS creation and capture, rollback removed
all observed fixed resources, and repeated stop returned success.

### F-013-08 — High: native observer cannot read the source PCAP

The remediated native profile captured 59 packets on `sdr-cap`, proving that
OVS/SPAN and tcpdump were active. The resulting `sdr-node.pcap` was
`root:root` mode 0600. The packet observer runs as the host UID/GID and could
not read that file, leaving `packet-history.sqlite` with zero records and the
PCAPNG at its 48-byte header. Startup failed with `FAIL: OVS-observed counters
did not advance` and then rolled back cleanly.

**Remediation:** arrange source-PCAP ownership or group access so the bounded
non-root observer can read files created and rotated by native tcpdump. Verify
permissions after rotation, live counters, history, PCAPNG, and shutdown flush.

**Linux-host implementation: CLOSED.** Native tcpdump now drops to the invoking
operator after opening `sdr-cap`. The source PCAP was mode 0600 and owned by the
operator; OVS-derived counters advanced, history was queryable, PCAPNG was
produced, and two native cycles cleaned up exactly.

### F-013-09 — High: expanded SDR automated test fails on the Linux host

`python3 tests/test_sdr_example.py .`, the focused CTest, the complete CTest
run, and `scripts/verify.sh quick` all fail while waiting for the positive mTLS
control path. The server repeatedly reports
`SSLV3_ALERT_UNSUPPORTED_CERTIFICATE`, then the test raises `AssertionError:
mutual-TLS SDR control server did not become ready`. OpenSSL strict purpose
verification reports both generated certificates as valid, and the same mTLS
path succeeds inside the Docker runtime, so the host-Python test setup or its
certificate interaction remains non-portable.

**Remediation:** isolate each TLS negative case and context from the positive
control path, make server readiness deterministic, and prove the expanded test
on both supported macOS and Linux host Python/OpenSSL combinations.

**Linux-host implementation: CLOSED.** `control_server` exposes an optional
readiness event after loading its TLS context. Tests wait for that event before
switching process-wide credentials. Direct execution and the registered focused
CTest pass all positive and negative cases.

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

### 9.2 Remediation revalidation at `6d1b36f`

| Command/check | Result |
|---|---|
| complete CTest inventory | **FAIL** — 33/34; only `graphx-sdr-node-portable` failed |
| `scripts/verify.sh quick` | **FAIL** at the same CTest; log `outputs/verification/20260907T002905Z-quick.log` |
| portable default and `--no-capture --no-history` | **FAIL** — tcpdump permission denied; both failures rolled back to zero resources |
| stale occupied port plus explicit free override | **PASS** — override honored; no `NameError`; rollback clean |
| invalid and occupied direct preflight | **PASS** — bounded actionable diagnostics |
| wrong-owner native Docker network fixture | **PASS** — cleanup refused with exit 2 and preserved the fixture |
| native OVS/SPAN start | **FAIL after capture** — 59 packets captured; observer could not read root-owned mode-0600 PCAP; counters stayed zero |
| native rollback and repeated stop | **PASS** — all observed fixed resources absent and both stops succeeded |

### 9.3 Linux-host implementation completion

| Command/check | Result |
|---|---|
| `python3 tests/test_sdr_example.py .` and focused CTest | **PASS** — protocol, endpoint, framing, schema, certificate, replay, lifecycle, and static security cases |
| portable default cycle | **PASS** — counters, history, PCAPNG, GUI pause/resume, mTLS, tune, and cleanup |
| portable opt-out cycle | **PASS** — live counters/control remain active; capture/history APIs report disabled; repeated cleanup succeeds |
| native default OVS/SPAN cycle | **PASS** — counters and history advance, control/tune pass, source PCAP is operator-owned, cleanup exact |
| native opt-out OVS/SPAN cycle | **PASS** — counters/control remain active; capture/history APIs report disabled; repeated cleanup succeeds |
| `scripts/verify.sh full` | **PASS** — 340 s; three 34/34 CTest runs and Docker feature suite; log `outputs/verification/20260907T003629Z-full.log` |
| final host cleanup | **PASS** — no SDR containers, Docker networks, fixed links, namespace, OVS bridge, or owned processes remain |

One initial manual Compose render omitted exported state variables and one
inspection command incorrectly attempted to parse human-readable output as
JSON. Both were verifier setup errors; corrected repetitions passed and neither
is attributed to the product.

### 9.4 Updated-repository revalidation at `9d60238`

The current verifier accepted the checked-in Linux-native results as direct
operator runtime evidence and independently reran the portable/macOS portion
against the updated repository revision.

| Command/check | Result |
|---|---|
| clean baseline and `git diff --check` | **PASS** — `main` at `9d60238`; no pre-existing working-tree changes |
| `scripts/verify.sh portable` | **PASS** — C++23 34/34, C++20 34/34, all checked-in configurations, telemetry 76/76, GUI 14/14, and portable feature suite; 60 s; log `outputs/verification/20260907T004720Z-portable.log` |
| focused SDR Python and five focused CTests | **PASS** — parser, endpoint, framing, mTLS/certificate, lifecycle/static, QEMU, documentation, and both SDR configurations |
| simulated default lifecycle on port 28113 | **PASS** — start/status/verify/control/stop, live counters 181/181 to 195/195 without refresh, all tabs including History → Network, and repeated cleanup |
| default capture/history inspection | **PASS** — Ethernet PCAPNG, 3,393 packets, strict time order, independently decoded UDP/TCP ports, and 744 SQLite packet-history rows |
| simulated opt-out lifecycle on port 28114 | **PASS** — live observation/control remained active, capture catalog reported disabled, history returned 503 disabled, and repeated cleanup succeeded |
| occupied port, invalid port, and external-on-macOS rejection | **PASS** — actionable exits 1, 1, and 2 respectively; no residual Phase 13 containers or networks |
| configuration validation, Compose render, and native infra dry-run | **PASS** after supplying the launcher's required render environment; dry-run remains inspection-only |
| `scripts/verify.sh quality` | **PASS** — format and static analysis; log `outputs/verification/20260907T005022Z-quality.log` |
| `scripts/verify.sh sanitizers` | **PASS** — LLVM 21 UBSan 34/34 plus sanitizer coverage; package test disabled by profile; log `outputs/verification/20260907T005128Z-sanitizers.log` |
| `scripts/verify.sh fuzz` | **PASS** — bounded envelope and frame fuzz runs, 30 seconds each; log `outputs/verification/20260907T005154Z-fuzz.log` |

No control token was entered through browser automation. That credential was
instead exercised by the launcher's authenticated GUI-relay verification and
the direct mTLS controller workflow. Browser automation remained read-only and
confirmed rendering, live WebSocket updates, capture/history content, tab
transitions, and the absence of console errors.

## 10. Future native-Linux release re-verification

For a future release revalidation, rerun on a native Linux host with
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

- The original handoff and early verification snapshots predate the Linux
  remediation. Their failure language is retained as explicitly superseded
  history; the current acceptance matrix and section 9.4 govern present status.
- Port override, occupied/invalid-port handling, failed-start rollback, focused
  TLS readiness, UDP source-address policy, and capture ownership now pass.
- Documentation is internally consistent and accurately distinguishes the
  portable bridge, native OVS/SPAN, and optional physical attachment.
- The external topology and dry-run are not runtime OVS evidence.
- UDP remains deliberately unauthenticated/unencrypted/unreliable; endpoint
  filtering reduces accidental/spoofed acceptance but does not authenticate a
  physical SDR.
- Fixed-name native resources remain operationally sensitive, so future changes
  must retain ownership labels, refusal tests, and exact-cleanup inspection.
- The only current non-blocking quality advisory is the large production GUI
  bundle. A real physical SDR and RF behavior remain outside this phase's proof.

## 12. Exit decision and prioritized remediation

**Overall result: ACCEPTED.** Phase 13 may enter the accepted chain.

All findings that blocked Linux-host acceptance are closed by implementation and
direct runtime evidence. Historical failed runs remain above for traceability.

Next verification work:

1. rerun the macOS portable lifecycle/adversarial cases after remediation;
2. run the portable simulated profile and regressions on Linux; and
3. run the complete native-Linux OVS/SPAN, interruption, ownership, security,
   capture/history, GUI, repeat-start, and exact-cleanup matrix.

Later enhancement:

- split the large GUI production bundle and retain this as non-blocking quality
  work.

Phase 13 is marked `ACCEPTED` because the findings are closed with direct
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
- Updated-repository portable run:
  `outputs/sdr-node/simulated/20260907T004837Z`
- Updated-repository verification logs:
  `outputs/verification/20260907T004720Z-portable.log`,
  `outputs/verification/20260907T005022Z-quality.log`,
  `outputs/verification/20260907T005128Z-sanitizers.log`, and
  `outputs/verification/20260907T005154Z-fuzz.log`

## Appendix B — independent evidence boundaries

The macOS Docker engine reports Linux/arm64, but it runs inside Docker Desktop.
It is valid evidence for the portable container profile only. It is not a
native-Linux host and provides no evidence for host OVS, SPAN, macvlan parent,
network namespace, `/proc` process ownership, Linux capabilities, physical NIC
safety, or exact host teardown. Likewise, generated namespace traffic on Linux
will be evidence for the disposable simulator and network path, not for a real
SDR's firmware, timing, RF behavior, or production certificate lifecycle.
