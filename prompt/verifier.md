# GraphX independent verification work package

Independently verify **Phase 14: static-route and deny-policy laboratory** in
`~/workspace/graphx-docker` against `prompt/implement.md`, the repository, and
acceptance identifiers ROUTE-001 through ROUTE-010. Read
`phase_14_handoff.md` only as an implementation claim. Write the result to
`phase_14_verification.md`.

## Independence and evidence

- Read the complete contracts, roadmap, repository state/history, schema,
  implementation, tests, architecture, examples, and operator documentation.
- Do not make product fixes, discard work, commit, publish, attach a physical
  interface, or trust a handoff, filename, dry-run, or claimed log as proof.
- Derive the traceability matrix independently and map every status to direct
  implementation evidence plus the required runtime evidence.
- Classify checks as portable runtime, native-Linux runtime, automated simulated
  dependency, inspection only, blocked, not applicable, or failed.
- Portable configuration and dry-run output cannot prove kernel, OVS, namespace,
  nftables, capture, privilege, ownership, rollback, or cleanup behavior.

## Verification procedure

### 1. Baseline and compatibility

Record OS/kernel/architecture, Docker/Compose, OVS, iproute2, nftables,
tcpdump/TShark, privileges, compilers/build tools, and repository state. Run the
existing portable acceptance baseline and focused prior-example regressions.
Confirm configuration version and existing public behavior remain compatible.

### 2. Configuration and planned topology

Validate, inspect, and project the Phase 14 configuration. Independently compare
declared domains, addresses, router interfaces, static route, ordered policies,
mirrors, and every `edge_path` with create/status/destroy dry-run output.
Adversarially test duplicate identifiers, bad references, overlapping or invalid
subnets/addresses, invalid next hops/devices, missing switches, incomplete paths,
policy action/order changes, unknown keys, and bounds.

### 3. Native realization and flow states

On native Linux, record relevant host routes, rules, namespaces, links, Docker
networks, processes, nftables, and OVS state before mutation. Start the lab and
inspect every declared object directly. Prove with receiver and packet evidence:

1. the allowed left-to-middle datagram arrives;
2. the middle-to-left datagram does not arrive and its deny counter advances;
3. the left-to-right datagram initially fails because the declared endpoint
   route is absent, not because of policy or an application error;
4. `apply-route` installs exactly the declared route and only the third outcome
   changes to success; and
5. `clear-route` removes it and restores the original classification.

Repeat the complete lifecycle twice. Validate OVS mirrors and retained PCAPs
with an independent reader and correlate frames with diagnostic receipt logs and
nftables counters.

### 4. GUI and diagnostic honesty

Use a real browser to inspect every relevant tab and History to Network
transitions. Confirm the topology and ordered paths are accurate; allowed,
policy-denied, missing-route, and route-applied states are visually distinct;
live changes occur without refresh; and no policy or routing condition is called
an application failure. Confirm diagnostic events are bounded and allow-listed.

### 5. Lifecycle ownership and failure injection

Test missing tools/privilege/CLI, occupied names/subnets/addresses, stale and
forged state, wrong-owner fixtures, duplicate resources, invalid next hops,
capture failure/limit, endpoint failure, interruption after each startup stage,
partial cleanup, repeated start/stop/apply/clear, and immediate restart. Every
failure must be actionable, bounded, roll back owned resources, and preserve
unrelated state.

After stop, compare the host baseline and prove that all owned `gx-route-*`
namespaces, links, OVS bridges/ports/mirrors, nftables objects, processes, and
listeners are gone while retained evidence remains readable by the operator.

### 6. Regression and documentation

Run unit/CTest, configuration/schema, projection, telemetry/web, formatting,
static analysis, sanitizer/fuzz gates proportional to changes, and all affected
existing examples. Review architecture source/DOCX, ADR index, example indexes,
network/GUI guides, test procedure/reference, changelog, support, and
compatibility text for accurate commands, risks, limits, and evidence labels.

## Acceptance matrix and verdict

Report ROUTE-001 through ROUTE-010 with requirement, implementation path and
evidence, validation evidence, status (`Implemented`, `Partial`, `Missing`, or
`Not Yet Applicable`), and precise remediation. Also report commands/results,
architectural drift, stale documentation, missing tests, security/resource
risks, and environmental limitations.

Phase 14 passes only when the native Linux laboratory completes two repeated
baseline/apply/clear/cleanup cycles, the deny and missing-route classifications
are independently proven, and host state returns to its baseline. Portable
dry-run and macOS checks are supporting evidence only; without native Linux the
overall result is incomplete.
