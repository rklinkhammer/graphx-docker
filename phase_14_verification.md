# Phase 14 Independent Verification Report

**Verdict: CHANGES REQUIRED — mandatory native Linux acceptance fails**

**Verification date:** 2026-09-07  
**Repository:** `/home/rklinkhammer/workspace/graphx-docker`  
**Branch / baseline:** `main` / `90c25840d6cc11419409b47178ac19bb85ff359a`  
**Verified state:** the checked-in Phase 14 implementation; this verification did not change product code  
**Contract:** `prompt/verifier.md`, acceptance identifiers ROUTE-001 through ROUTE-010

**Current independent verdict:** CHANGES REQUIRED. The 2026-09-07 remediation
re-verification below supersedes the original runtime results. F-014-01,
F-014-02, F-014-03, F-014-04, F-014-06, and F-014-07 are closed. F-014-05
remains open because an infrastructure-create failure before ownership marking
leaves a fixed-name resource behind and rollback refuses to remove it.

## 1. Executive summary

Phase 14 is not accepted on Linux. The portable model, planner, telemetry, web,
quality, sanitizer, fuzz, Docker, QEMU, and prior-example regression surfaces
pass. The native route-policy launcher also creates the three OVS domains, the
router namespace and interfaces, forwarding, nftables policy, and all three
mirrors. It then fails before any flow or GUI acceptance because privileged
`dumpcap` cannot create `route-policy.pcapng` in the operator-owned mode-0755
run directory:

```text
dumpcap: The file to which the capture would be saved
(".../route-policy.pcapng") could not be opened: Permission denied.
dumpcap failed to start on the three OVS mirror ports
Route-policy startup failed; rolling back owned resources
```

This is a product/runtime defect rather than a missing host prerequisite.
`dumpcap` starts, resolves all three mirror interfaces, and reaches output-file
creation. The required baseline/apply/clear sequence, independent receiver and
packet proof, retained PCAP inspection, browser transitions, and second native
cycle therefore cannot execute.

Rollback removed the observed Phase 14 links, namespaces, processes,
containers, and fixed-name resources. Runtime evidence was retained under
`outputs/static-route-policy/20260907T013537Z`. The untracked `.state` and
output directories were preserved as evidence.

Independent adversarial and source review found six additional correctness,
diagnostic-integrity, ownership, and presentation gaps. The most important are
that a sender failure can be reported as a proven missing-route outcome and
that telemetry accepts contradictory diagnostic state/evidence pairs.

## 2. Evidence classification

- **Native Linux runtime — failed:** infrastructure realization reaches live
  OVS mirrors, but capture startup fails before flow verification.
- **Portable automated/runtime — passed:** configuration, projection planning,
  C++20/C++23 tests, telemetry, web, quality, sanitizers, fuzzing, Docker, QEMU,
  SDR, and existing examples.
- **Inspection/adversarial — failed:** duplicate policy identifiers validate;
  negative-flow, exact-route, diagnostic-pair, rollback, and GUI-style gaps are
  present in the controlling code paths.
- **Blocked by the native product failure:** two complete lifecycle cycles,
  flow transitions, independent PCAP correlation, live browser inspection,
  capture ceiling, and the full failure-injection/canary matrix.
- **Not applicable:** physical network or SDR hardware; the Phase 14 laboratory
  uses namespaces, OVS, and local virtual interfaces.

The implementation handoff and macOS logs were used only as test leads, not as
acceptance proof.

## 3. Environment and baseline

| Item | Verified value |
|---|---|
| Host | Ubuntu 26.04.1 LTS, x86_64 |
| Kernel | Linux 7.0.0-31-generic |
| CMake / GCC / LLVM | CMake 4.2.3; GCC 15.2.0; LLVM 21.1.8 |
| Python / OpenSSL | Python 3.14.4; OpenSSL 3.5.5 |
| Docker / Compose | Docker 29.1.3; Compose 2.40.3 |
| OVS / iproute2 / nftables | OVS 3.7.1; iproute2 6.19.0; nftables 1.1.6 |
| Capture tools | dumpcap/TShark 4.6.4; tcpdump 4.99.6 |
| QEMU | 10.2.1 |
| Privilege | operator is in `sudo` and `docker`; native run used authenticated sudo |
| Repository | `main` at `90c25840d6cc11419409b47178ac19bb85ff359a` |

The initial repository state was clean except for retained, untracked Phase 14
runtime state and evidence. No product files were modified by verification.

## 4. Findings

### F-014-01 — High — native capture cannot create its output PCAPNG

`start_capture` launches `dumpcap` through `sudo`, targeting a file inside the
operator-owned run directory. On this host dumpcap opens the interfaces but
drops the access needed to create that file. No PCAPNG is created, startup
times out, and the launcher rolls back.

**Impact:** ROUTE-002 through ROUTE-008 cannot obtain mandatory runtime proof;
neither required lifecycle cycle completes.

**Evidence:** `outputs/verification/20260907T013249Z-native-linux.log` and
`outputs/static-route-policy/20260907T013537Z/dumpcap.log`.

**Required remediation:** establish a least-privilege capture/output ownership
model that permits dumpcap to create and rotate the bounded capture while
leaving the retained PCAP readable by the invoking operator. Add a Linux test
that proves creation, readability, rotation/limit behavior, and cleanup.

### F-014-02 — High — missing-route proof ignores sender failure

The route-absent branch in `verify_flows` checks only that the receiver failed.
It records `missing-route` even if the sender itself returned nonzero. This can
misclassify an application, bind, namespace, or local send failure as routing
evidence.

**Impact:** ROUTE-003 and ROUTE-005 do not prove that the third flow failed
specifically because the route was absent.

**Required remediation:** require sender success in both positive and negative
network outcomes, retain the sender diagnostic, and add a fault-injection test
that forces sender failure and requires verification to fail closed.

### F-014-03 — High — contradictory diagnostic evidence is accepted

Telemetry independently allow-lists four states and four evidence strings but
does not enforce valid pairings. For example, `policy-denied` with
`route-installed` passes validation and is projected to the API and GUI.

**Impact:** ROUTE-005 and ROUTE-008 can publish internally contradictory state
as operator-visible truth.

**Required remediation:** validate explicit state/evidence pairs and their
relationship to `routeApplied`; add malformed and contradictory integration
cases that fail closed.

### F-014-04 — Medium — duplicate policy identifiers are accepted

An adversarial configuration with two `deny-middle-left` policy IDs validated
successfully. Policy IDs become nftables comments, while deny verification
selects the first matching comment. Counter attribution is therefore ambiguous.

**Impact:** ROUTE-001 and ROUTE-004 cannot guarantee that the counter belongs to
the intended named policy.

**Required remediation:** require policy IDs to be unique within a router and
add parser/planner negative tests.

### F-014-05 — Medium — failed-start cleanup can bypass ownership validation

Cleanup skips `native_resources_owned` while
`current_start_owns_native=true`. That flag is set before infrastructure
creation and remains true through later startup stages. A same-name resource
replaced by another actor during that window may be deleted by rollback without
rechecking its ownership marker.

**Impact:** ROUTE-006 and ROUTE-007 do not fully establish preservation of
unrelated host state under races or partial replacement.

**Required remediation:** validate ownership at deletion time for every
resource, including failed-start rollback, and exercise replacement/collision
faults with unrelated canaries.

### F-014-06 — Medium — route-state check is not exact

`route_present` matches only destination `10.64.30.10/32`; it does not verify
the declared next hop `10.64.3.10` and device `rt-right`.

**Impact:** ROUTE-003 and ROUTE-005 may classify an unrelated route to the same
destination as the declared manual route.

**Required remediation:** compare destination, next hop, and device against the
declared route and add a wrong-next-hop/wrong-device native negative test.

### F-014-07 — Low — allowed and route-applied are not visually distinct

The GUI assigns `diagnostic-allowed` and `diagnostic-route-applied` identical
edge and badge styling. Labels differ, but the documented visual distinction
does not.

**Impact:** ROUTE-005 and the operator guide overstate visual differentiation.

**Required remediation:** give all four states distinct accessible visual
semantics and add a rendered-state assertion or browser screenshot check.

## 5. Acceptance matrix

| ID | Requirement | Independent evidence | Status | Required action |
|---|---|---|---|---|
| ROUTE-001 | Three domains, router, route, ordered policies, mirrors, complete paths | Model/schema, inspect and dry-run output pass; duplicate policy IDs do not fail | **Partial** | Enforce policy-ID uniqueness and rerun adversarial configuration cases. |
| ROUTE-002 | Native realization matches declaration | Native run creates bridges, ports, router interfaces, forwarding, nftables, and mirrors, then capture fails | **Partial** | Repair capture startup and independently inspect all live objects. |
| ROUTE-003 | Missing route, exact apply success, clear restores failure | Apply/clear plans are exact; native transition never runs; negative proof and route check are incomplete | **Partial** | Require sender success, verify exact route tuple, and complete two native transitions. |
| ROUTE-004 | Distinct deny policy with named counter proof | Planner and nft rule creation exist; native probe never runs; duplicate IDs make attribution ambiguous | **Partial** | Make IDs unique and prove receiver timeout plus the exact named counter twice. |
| ROUTE-005 | Honest logs, capture, telemetry, and GUI states | Portable telemetry/web tests pass; contradictory pairs and identical styling remain; browser runtime is blocked | **Partial** | Validate pair semantics, differentiate states, and perform live browser/PCAP correlation. |
| ROUTE-006 | Repeatable lifecycle and safe rollback | Capture failure rolls observed resources back; two cycles and staged failures are unproven; ownership race remains | **Partial** | Close ownership gap and complete lifecycle/failure-injection matrix. |
| ROUTE-007 | Preserve unrelated host state | No observed Phase 14 residue after rollback; full canary matrix is blocked and rollback guard can be bypassed | **Partial** | Recheck per-resource ownership and prove canary preservation across two cycles and failures. |
| ROUTE-008 | Bounded inputs, waits, capture, diagnostics, cleanup | Parser/file bounds and fuzz/sanitizers pass; capture limit cannot run; contradictory diagnostics are accepted | **Partial** | Enforce semantic bounds and prove capture ceiling/readability and forced cleanup. |
| ROUTE-009 | Compatibility and regression coverage | Full Linux quality, ASan/UBSan, fuzz, portable, Docker, QEMU, SDR, and existing examples pass | **Implemented** | Retain these gates in the remediation rerun. |
| ROUTE-010 | Accurate architecture, operator, and verifier documentation | Required documentation exists and consistency test passes; native success and visual-distinction claims are not yet true | **Partial** | Update claims as needed and verify all documented commands after remediation. |

## 6. Commands and results

| Command or check | Result | Classification |
|---|---|---|
| `GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/verify.sh native-linux` | **FAIL** at Phase 14 dumpcap startup; preceding portable/native-network and Phase 13 SDR gates pass | Native Linux runtime |
| Focused CTest: documentation, static-route config/portable, QEMU static, SDR portable | **PASS**, 5/5 | Automated regression |
| `python3 tests/test_static_route_policy.py . build/dev/graphx` | **PASS** | Portable Phase 14 contract |
| Phase 14 `graphx validate`, `inspect`, and infra/route dry-runs | **PASS**; route apply/clear plan is correct | Portable/inspection |
| Duplicate-policy adversarial validation | **FAIL EXPECTATION**: duplicate ID accepted with exit 0 | Adversarial configuration |
| `scripts/verify.sh full` | **PASS** | Linux quality, ASan/UBSan, fuzz, portable, Docker regression |
| Post-failure resource audit | No Phase 14 links, namespaces, processes, or containers observed; privileged OVS/nft state was also clean in the immediate authenticated audit | Native cleanup inspection |

Primary logs:

- `outputs/verification/20260907T013249Z-native-linux.log`
- `outputs/verification/20260907T014208Z-full.log`
- `outputs/static-route-policy/20260907T013537Z/dumpcap.log`

The `graphx project --check` attempt against `config/` was a verifier target
mismatch: that directory is the root project projection, not a Phase 14 example
projection. It is not a product finding.

## 7. Cleanup and limitations

The failed startup's rollback removed the route-policy namespaces and links,
capture process, containers, and observed fixed-name resources. Evidence files
remain operator-readable except for the absent PCAPNG. A later unprivileged
audit could not directly query the root-owned OVS database or nftables state;
the immediate authenticated post-failure audit found no Phase 14 objects.

Because F-014-01 prevents a running lab, this pass did not claim receiver,
counter, exact route-transition, PCAP, browser, two-cycle, capture-bound, or
complete canary/failure-injection evidence. These are mandatory remediation
revalidation gates, not optional follow-up work.

## 8. Reverification gate

Phase 14 can be reconsidered only after the findings above are remediated and a
fresh native Linux run proves both complete baseline/apply/clear/cleanup cycles.
The rerun must independently correlate sender and receiver results, exact kernel
route tuples, the unique named nftables counter, all three OVS mirrors, readable
bounded PCAPNG evidence, live browser states, ownership/canary preservation,
and an exact final host baseline. The full Linux regression profile must remain
green.

## 9. Independent remediation re-verification — 2026-09-07

### 9.1 Verdict

**CHANGES REQUIRED — native flow acceptance passes, but partial-create rollback
remains unsafe and incomplete.**

This independent pass verified the remediated working tree based on
`90c25840d6cc11419409b47178ac19bb85ff359a`. No product code was changed during
re-verification. The original failed-capture narrative above remains historical
evidence; the results in this section are current.

### 9.2 Closed findings

- **F-014-01 closed:** dumpcap streams pcapng to an operator-created file. Both
  retained captures are `rklinkhammer:rklinkhammer`, mode 0644, 21–24 KiB,
  readable by independent Wireshark tools, and contain `rtl-cap`, `rtm-cap`, and
  `rtr-cap` plus UDP ports 18601, 18602, and 18603.
- **F-014-02 closed:** every negative routed outcome had a separate successful
  sender log and receiver timeout. Each cycle retained 12 sender and 12 receiver
  logs across start, verify, apply, and clear.
- **F-014-03 closed:** telemetry rejects contradictory state/evidence pairs and
  inconsistent `routeApplied`; the focused integration test passes.
- **F-014-04 closed:** a duplicate policy ID now fails validation with exit 2
  and identifies both duplicate policy paths.
- **F-014-06 closed:** a route through `10.64.3.254` was not classified as the
  declared route. Apply replaced it with exactly `10.64.30.10/32 via
  10.64.3.10 dev rt-right`; clear removed it.
- **F-014-07 closed:** a real browser observed green allowed, red denied, amber
  missing-route, and blue dashed route-applied states. Application and Network
  views updated without refresh; History-to-Network navigation worked. History
  returned 503 as documented because this laboratory disables packet history.

### 9.3 Remaining finding

#### F-014-05 — High — create-stage failure leaves an unmarked native resource

A verifier-supplied GraphX executable created `rtl-ovs` and returned exit 23
during `infra create`, before `mark_native_ownership` could run. Startup invoked
rollback, but cleanup reported:

```text
Route-policy startup failed; rolling back owned resources
Refusing to remove route-policy resources without matching ownership markers
```

`rtl-ovs` remained present until the verifier removed that exact dummy fixture.
The behavior fails the contract requirement that interruption or failure after
each startup stage roll back resources created by the run. It also leaves a
fixed-name collision that blocks immediate restart. Refusing to delete an
unmarked object is appropriately conservative, but ownership must be established
atomically as resources are created, or the create operation must provide its
own transactional rollback.

An independently created fixed-name `rtl-ovs` canary was preserved by preflight,
and namespace, short-name link, OVS bridge, nftables table, and blackhole-route
canaries all survived two successful cycles. Those positive cases do not close
the pre-marker failed-create gap.

### 9.4 Native runtime evidence

Two complete independent cycles passed with unrelated canaries present:

1. baseline allowed delivery, denied receiver timeout plus named nftables
   counter increase, and routed sender success plus receiver timeout;
2. exact route apply and routed receiver delivery;
3. route clear and restored routed timeout;
4. stop and repeated stop;
5. canary preservation and zero owned namespace, link, OVS, nftables, container,
   capture-process, listener, or route residue.

Cycle evidence:

- `outputs/static-route-policy/20260907T022609Z`
- `outputs/static-route-policy/20260907T022737Z`

The live inspection confirmed router addresses `10.64.1.1/24`,
`10.64.2.1/24`, and `10.64.3.1/24`; endpoint addresses and routes; all three
OVS bridges, ports, all-traffic mirrors, and common ownership token; forwarding;
ordered nftables allow/deny/allow rules; exact route absence/apply/clear; and
API diagnostic classifications.

### 9.5 Regression and adverse results

| Check | Current result |
|---|---|
| `scripts/verify.sh full` | **PASS** in 342 seconds; `outputs/verification/20260907T022819Z-full.log` |
| Focused Phase 14 portable, telemetry, and web suites | **PASS** |
| Duplicate policy identifier | **PASS**: rejected with exit 2 |
| Wrong next hop | **PASS**: remained `missing-route` until exact apply |
| Invalid and occupied GUI port | **PASS**: actionable preflight refusal, no native mutation |
| Occupied fixed-name link | **PASS**: unrelated canary preserved |
| Namespace/link/OVS/nftables/route canaries | **PASS** across both complete cycles |
| Live Application/Network GUI transition | **PASS** without refresh; four distinct states |
| Failed `infra create` after first link | **FAIL**: partial unmarked link survives rollback |
| Final host comparison | **PASS** after verifier fixture removal; no Phase 14 or canary residue |

### 9.6 Current acceptance matrix

| ID | Current status | Independent conclusion |
|---|---|---|
| ROUTE-001 | **Implemented** | Model, strict validation, unique policy IDs, paths, route, policy order, and mirror plan pass. |
| ROUTE-002 | **Implemented** | Native namespaces, addresses, links, OVS bridges/ports/mirrors, forwarding, and nftables match the declaration. |
| ROUTE-003 | **Implemented** | Sender/receiver evidence and exact route tuple prove missing/apply/clear transitions twice. |
| ROUTE-004 | **Implemented** | Unique named deny rule, receiver timeout, and increasing counter are proven twice. |
| ROUTE-005 | **Implemented** | PCAP, logs, strict diagnostics, API, and live browser states correlate honestly. |
| ROUTE-006 | **Partial** | Successful lifecycle and repeated stop pass; pre-marker failed-create rollback leaves residue. |
| ROUTE-007 | **Partial** | Unrelated canaries survive normal/collision paths, but failed-create residue prevents exact automatic restoration. |
| ROUTE-008 | **Partial** | Inputs, waits, diagnostics, capture, and successful cleanup are bounded; failed-create cleanup is incomplete. |
| ROUTE-009 | **Implemented** | Full Linux quality, sanitizer, fuzz, portable, Docker, QEMU, SDR, and prior-example regressions pass. |
| ROUTE-010 | **Implemented** | Commands, visual states, evidence labels, and current verification status are documented. |

### 9.7 Required remediation

Make infrastructure creation ownership-aware or transactional from its first
mutation. A failure after any create command must remove only resources created
by that invocation, preserve same-named unrelated replacements, and permit an
immediate restart. Add automated fault injection at multiple create stages and
verify both exact cleanup and canary preservation. Then rerun the focused
failure matrix, two native cycles, live browser checks, and full Linux profile.