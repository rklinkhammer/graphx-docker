# Phase 14 implementation handoff

Date: 2026-09-06  
Candidate baseline: `a9ee38b131ebd858fba825cc051179a79559b5a9` plus this working tree  
Implementation host: macOS 26.6.2, Apple Silicon  
Phase status: **implementation complete; independent native-Linux verification required**

## Outcome

Phase 14 now provides `examples/static-route-policy`, a three-domain native
Linux laboratory with one allowed UDP flow, one nftables-denied reverse flow,
and one flow that transitions from missing-route to delivered only through an
exact declared manual route. The same topology is available for portable model
validation and dry-run inspection. Telemetry and both GUI topology views expose
the four allow-listed diagnostic states without representing raw packets as
GraphX messages or history.

This handoff does not claim native runtime acceptance. The implementation host
cannot prove Linux namespaces, system-datapath OVS, nftables, kernel routes,
SPAN capture, packet outcomes, cleanup, or unrelated-host-state preservation.

## Requirement traceability

| ID | Requirement | Implementation evidence | Validation evidence | Status | Verifier/remediation |
|---|---|---|---|---|---|
| ROUTE-001 | Three domains, explicit router, route, ordered policies, OVS mirrors, complete paths | `examples/static-route-policy/graphx.yaml`; strict loader/schema; ADR 0015 | `graphx-config-tests`, `graphx-config-static-route-policy`, portable route-policy test | Implemented | Compare the loaded model and native state. |
| ROUTE-002 | Native realization matches declaration | `src/infra.cpp`; `scripts/demo.sh start/status`; endpoint namespace setup | Command dry-runs only on macOS | Partial | Native Linux must inspect namespaces, addresses, links, bridges, mirrors, forwarding, and rules. |
| ROUTE-003 | Missing route → apply success → clear failure | `install: manual`; `graphx infra route apply/clear`; bounded probes | Exact apply/clear command tests; invalid/duplicate route rejection | Partial | Native Linux must prove receiver outcomes and exact route state in both directions of the transition. |
| ROUTE-004 | Deny policy is distinct and counter-proven | Ordered `deny-middle-left`; `deny_packets`; one-way probe | Config/planner tests and policy-order source assertion | Partial | Native Linux must prove receiver timeout plus increasing named nft counter while allowed flows still pass. |
| ROUTE-005 | Honest logs, capture, telemetry, and GUI states | Per-flow logs; bounded PCAPNG; strict diagnostic JSON; telemetry merge; edge CSS/inspector | 77 telemetry tests and 15 web tests pass; new evidence/state tests pass | Partial | Real browser and packet capture must be checked during native flow transitions. |
| ROUTE-006 | Repeatable lifecycle and safe rollback | preflight, random owner token, OVS external IDs, interface aliases, trap rollback, repeated stop; two-cycle native profile | Shell/static safety tests only | Partial | Execute two cycles, occupied/stale/forged/partial/interrupted cases, and repeated stop. |
| ROUTE-007 | Preserve unrelated host state | fixed names/ranges, collision refusal, exact resource cleanup, ownership verification | Code inspection only | Partial | Use canary namespace/link/bridge/route/nft table and baseline/final snapshots on Linux. |
| ROUTE-008 | Bounded inputs, waits, files, capture, diagnostics, cleanup | 128-byte probe token, short deadlines, 64 MiB capture, strict 64 KiB descriptor-based JSON read, bounded loops | Parser boundaries, malformed/oversized evidence, static script assertions | Implemented for portable surfaces | Verify capture ceiling, permissions, and forced-failure cleanup on Linux. |
| ROUTE-009 | Backward compatibility and regression coverage | default `install: create`; version remains 1; existing route behavior unchanged | Quick 36/36; portable C++20/C++23 36/36 each; quality; UBSan; QEMU/SDR tests; Docker image build | Implemented for tested surfaces | Run Linux ASan+UBSan, native network suite, and any release-required Docker gates. |
| ROUTE-010 | Architecture/operator/verifier documentation | example README, architecture Markdown/DOCX, network/GUI/test guides, ADR, changelog, prompt contracts | documentation-consistency test; DOCX rendered and visually inspected | Implemented | Verify commands and expected states against the native run. |

## Main implementation paths

- Model and native lifecycle: `examples/static-route-policy/`
- Route configuration and CLI: `include/graphx/network.hpp`,
  `include/graphx/infra.hpp`, `src/config.cpp`, `src/infra.cpp`,
  `apps/cli/main.cpp`, `apps/cli/projection.cpp`, and the JSON Schema
- Telemetry/GUI: `apps/telemetry/server.mjs`,
  `web/src/data/topology.js`, `web/src/components/TelemetryEdge.jsx`,
  `web/src/components/EdgeInspector.jsx`, and `web/src/styles.css`
- Automated tests: `tests/test_config.cpp`,
  `tests/test_static_route_policy.py`,
  `apps/telemetry/network-diagnostic.integration.test.mjs`, and
  `web/src/network-diagnostic.test.mjs`
- Contracts/decisions/docs: `prompt/implement.md`, `prompt/verifier.md`,
  `docs/adr/0015-explicit-manual-route-activation.md`, and the Phase 14
  sections in the architecture and operator guides

## Commands and results

| Command/gate | Result | Evidence classification |
|---|---|---|
| `scripts/verify.sh quick` | PASS, 36/36 CTest; `outputs/verification/20260907T012237Z-quick.log` | macOS build/runtime and portable tests |
| `scripts/verify.sh portable` | PASS, C++23 36/36, C++20 36/36, telemetry 77/77, web 15/15, web build; `outputs/verification/20260907T012307Z-portable.log` | macOS portable runtime/code evidence |
| `scripts/verify.sh quality` with Homebrew LLVM 21 format/tidy | PASS; `outputs/verification/20260907T012415Z-quality.log` | static analysis and formatting |
| `scripts/verify.sh sanitizers` | PASS, macOS 26 LLVM 21 UBSan profile, 36 enabled tests plus coverage; package test intentionally disabled; `outputs/verification/20260907T012446Z-sanitizers.log` | sanitizer runtime on macOS, not Linux ASan |
| Phase 14 Compose `config --quiet` | PASS | Compose model validation |
| Phase 14 telemetry image build | PASS; production bundle advisory at 1,859.35 kB (570–572 kB gzip) | Docker build evidence, no lab runtime |
| `graphx project graphx.yaml --output-dir config --check` | PASS, projections current | projection consistency |
| architecture DOCX generation/render | PASS, every rendered page inspected | document artifact/layout evidence |
| First sanitizer invocation with explicit compiler overrides | Failed before GraphX compilation because the override bypassed macOS SDK initialization (`time.h` unavailable); normal project invocation above passed | Superseded invocation error, not a product finding |

The production GUI bundle warning is non-blocking and predates the Phase 14
logic. It is retained as a later performance/code-splitting advisory rather
than treated as a correctness or acceptance failure.

## Native Linux verifier sequence

Use a dedicated Linux host and the exact candidate commit. Do not run the whole
suite as root.

1. Install/build the documented native prerequisites and record tool versions.
2. Review `prompt/verifier.md` and the example README. Record baseline routes,
   namespaces, links, OVS, nftables, Docker objects, and processes. Create the
   unrelated canary resources required by the verifier.
3. Run `GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/verify.sh native-linux`. This
   now includes two complete Phase 14 start/verify/apply/clear/stop cycles.
4. Independently inspect each intermediate kernel/OVS/nftables state, packet
   evidence, PCAP readability, and GUI Application/Network state instead of
   relying only on launcher PASS text.
5. Execute occupied name/subnet, invalid next hop/policy order, stale/forged
   state, partial start, interruption, repeated stop, and resource-bound cases.
6. Compare the final snapshot and canaries with baseline. Record exact commands,
   outputs, artifacts, limitations, and verdict in `phase_14_verification.md`.

## Remaining gate

Phase 14 remains **Partial** until the independent native-Linux procedure passes.
Portable/macOS results establish model compatibility and implementation quality;
they are not evidence for privileged Linux network behavior.

## Linux remediation implementation — 2026-09-07

The implementation was updated on Ubuntu 26.04.1 LTS in response to findings
F-014-01 through F-014-07 in `phase_14_verification.md`:

- dumpcap now streams pcapng to stdout so the unprivileged launcher creates and
   owns the bounded retained file;
- negative route proof requires sender success, and route presence compares the
   exact destination, gateway, and device tuple;
- sender and receiver evidence is retained in separate timestamped files for
   every probe attempt;
- diagnostic state/evidence pairs and `routeApplied` consistency fail closed;
- router policy identifiers must be unique;
- rollback rechecks native ownership markers before deletion, although later
   independent fault injection found that failures before marker installation
   leave partial resources behind;
- allowed and route-applied edges have distinct live badge and path semantics;
- the Phase 14 Compose tmpfs syntax and loopback publication work on Docker 29.

Two complete native cycles passed after the final runtime changes. Each cycle
proved baseline missing-route, named-policy denial, exact route apply and
delivery, route clear and restored timeout, repeated stop, operator-readable
three-interface PCAPNG evidence, complete sender/receiver logs, and zero final
Phase 14 host residue. A live Playwright check confirmed Application and Network
state updates without refresh and distinct blue dashed route-applied rendering.
The full Linux verification profile passed in 343 seconds.

Primary remediation evidence:

- `outputs/verification/20260907T020124Z-native-linux.log`
- `outputs/verification/20260907T020644Z-full.log`
- `outputs/static-route-policy/20260907T020552Z`
- `outputs/static-route-policy/20260907T020610Z`

This is implementer evidence, not an acceptance verdict. The subsequent
independent rerun closed six findings but kept F-014-05 open: create-stage
ownership or rollback must become transactional before Phase 14 can be
`ACCEPTED`.

## Transactional create implementation — 2026-09-07

F-014-05 is remediated in this working tree without changing the independent
verification verdict. `graphx infra create --transactional` now journals a
reverse operation immediately after each successful resource creation and
unwinds completed operations in reverse order if a later command fails. The
transaction covers veth pairs, OVS bridges, Linux network namespaces, and
external Docker networks. Transactional bridge creation is strict, so an
unrelated fixed-name bridge is never adopted. The Phase 14 launcher opts into
this mode before it installs its higher-level ownership markers; the legacy
nontransactional planner behavior remains unchanged for other callers.

Focused automated coverage proves reverse-order executor rollback, rollback
metadata on the Phase 14 plan, strict transactional bridge creation, CLI
dry-run behavior, and launcher use of `--transactional`. Native fault injection
through the real GraphX executor passed at these stages:

- command 4, after the first veth pair was created;
- command 25, after two OVS bridges and all endpoint/capture veth pairs existed;
- command 45, after the router namespace existed and its first interface had
   moved into that namespace.

Every injected failure returned status 23 and left zero Phase 14 links,
bridges, or namespaces. Namespace-stage rollback completed without rollback
errors after veth reversal was anchored on the peer that remains in the root
namespace. A preexisting dummy `rtl-ovs` canary caused strict creation to fail
and retained the same interface index and type. An immediate full native cycle
then passed missing-route, exact route apply/delivery, route clear/restored
timeout, named deny-counter proof, repeated stop, and zero owned residue.

Implementation evidence:

- `outputs/phase14-fault-25.log`
- `outputs/phase14-fault-45.log`
- `outputs/phase14-canary-collision.log`
- `outputs/static-route-policy/20260907T124324Z`
- `outputs/static-route-policy/20260907T125035Z`
- `outputs/phase14-transaction-cycle-2.log`
- `outputs/verification/20260907T124412Z-full.log` (`PASS`, 363 seconds)

The focused CTest selection, two complete native lifecycle cycles, and the
complete Linux profile pass. A fresh independent verification must supersede F-014-05 before
`phase_14_verification.md` or `verification_status.md` can mark Phase 14
accepted.

## Linux verification follow-up remediation — 2026-09-07

The subsequent independent pass found two remaining defects: rollback commands
were guarded only by resource names, and React Flow could calculate topology
edges before remounted node geometry was ready after leaving History.

Transactional creation now captures a stable identity immediately after each
successful mutation: root-link ifindex for veth pairs, bridge UUID for OVS,
mount inode for network namespaces, and Docker network ID. Before each reverse
operation, the executor obtains the identity again. Missing or changed objects
are skipped with an explicit diagnostic, preventing deletion of an unrelated
same-named replacement. The focused executor test replaces a created file with
a different object before failure and proves that rollback preserves it; the
Phase 14 planner test requires identity metadata for every rollback.

The topology component now withholds edges until asynchronous ELK node layout
has committed, then fits the viewport on the following animation frame. In a
rebuilt native console, the sequence Application to History to Network to
History to Application retained 3 application edges, 18 network hop edges, and
all diagnostic badges without reload. The blue dashed route-applied state also
survived the same History round trip.

Native replacement-race evidence is retained in
`outputs/phase14-remediation-replacement.log`; browser lifecycle evidence is in
`outputs/phase14-remediation-browser.log`; later-stage identity-checked rollback
is in `outputs/phase14-remediation-stage.log`. The complete Linux profile passed
in 362 seconds at `outputs/verification/20260907T131037Z-full.log`. These are
implementer results. The independent `CHANGES REQUIRED` verdict remains
unchanged until a fresh verifier supersedes F-014-05 and F-014-08.

## Identity-probe failure remediation — 2026-09-07

The latest independent Linux verification closed F-014-05 and F-014-08 but
found F-014-09: after a successful create command, identity-probe failure
returned before the new resource was journaled or any completed operation was
rolled back.

The infrastructure executor now uses one reverse-order rollback routine for
ordinary command failures and completed operations. If the identity probe for
a newly created resource fails, the executor first reverses that current
creation, then invokes the same identity-checked rollback routine for all
earlier journal entries. Probe status remains the command result, and rollback
failures retain explicit diagnostics.

Focused automated coverage creates two resources, forces the second identity
probe to return 23, and proves current-first reverse order plus zero residue.
The existing replacement test remains green and proves that completed entries
still skip rollback when their stable identity changes.

Native Linux injection replaced the first veth identity probe with status 23.
The real GraphX executor returned 23, logged the failed probe, executed
`ip link delete rtl-end`, and left neither `rtl-ovs` nor `rtl-end`. Evidence is
retained in `outputs/phase14-fix-identity-probe.log`.

Focused results:

- `graphx-config-tests` — pass, including identity-probe cleanup and
   replacement preservation;
- `graphx-config-static-route-policy` — pass;
- `graphx-static-route-policy-portable` — pass;
- `graphx-documentation-consistency` — pass.
- `scripts/verify.sh full` — pass in 362 seconds; evidence retained at
   `outputs/verification/20260907T133745Z-full.log`.

This is implementation evidence, not an independent acceptance decision.
`phase_14_verification.md` and `verification_status.md` retain `CHANGES
REQUIRED` until a fresh verifier closes F-014-09 and reruns the required native,
browser, packet, canary, cleanup, and full-profile gates.
