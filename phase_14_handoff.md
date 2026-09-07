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
