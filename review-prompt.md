# Review of the architectural review prompt

## Verdict

[`architectural-review-prompt.md`](architectural-review-prompt.md) is a strong,
repository-aware implementation-design prompt. It correctly constrains the work
to design artifacts, preserves the current runtime until review approval, names
the major architectural boundaries, and makes broad claims testable through
concrete scenarios and negative fixtures. It is suitable for commissioning a
substantial architecture package.

It is not yet fully deterministic. The largest weakness is that the prompt asks
the reviewer to treat configuration version 3 and `graphx.yml` as settled while
the repository's active decisions require version 2 and `graphx.yaml`. That is a
reasonable future-design premise, but the precedence and status of that premise
need to be explicit. The prompt also leaves “complete expected output” and the
required report/package inventory underspecified, which can produce reviews that
are individually plausible but difficult to compare or verify.

The prompt should be revised before use if comparable, mechanically reviewable
responses are important. None of these findings requires production changes.

## Evidence reviewed

- [`AGENTS.md`](AGENTS.md) defines the repository invariants, source ownership,
  verification expectations, and the requirement to read current project
  decisions.
- [`docs/project-decisions.md`](docs/project-decisions.md) records version 2 and
  `graphx.yaml` as the only current configuration contract.
- [`analysis.md`](analysis.md) clearly labels version 3, `graphx.yml`, proposed
  commands, and generated artifacts as illustrative future design.
- [`examples/README.md`](examples/README.md) confirms the 15 scenario families,
  their current execution environments, the native/OrbStack/Lima distinctions,
  and the externally managed QEMU source in network observation.
- [`docs/test-procedure.md`](docs/test-procedure.md) confirms that portable,
  Docker, privileged Linux/Lima, TAP lifecycle, and actual guest boot are
  separate kinds of evidence. It also states that the current QEMU launcher uses
  TCG and that no current launcher selects KVM.

## Findings

### F-01: Future-design authority is ambiguous

Severity: high

The prompt says to read and obey the current decisions, then declares I-01
accepted with version 3 and `graphx.yml`. The current authoritative documents
state that version 2 and `graphx.yaml` are the only accepted current format. The
design-only disclaimer prevents an immediate runtime contradiction, but it does
not define which authority controls the proposed contract when the reviewer
finds another conflict.

A reviewer could reasonably do either of the following:

- treat I-01 as an authorized future decision that overrides the current
  invariant only inside the design package; or
- reject the premise because `AGENTS.md` and the current decisions are declared
  authoritative.

Add an explicit precedence rule:

> For this design exercise only, the accepted I-01 through I-11 decisions are
> authorized future-state premises. They override conflicting current-state
> decisions only in proposed, clearly labeled design artifacts. Current source,
> tests, schemas, examples, and documentation remain unchanged and continue to
> describe version 2 until a separately approved implementation changes them.

This preserves the repository invariant while making the review assignment
unambiguous.

### F-02: “Complete expected output” has no acceptance definition

Severity: high

Deliverable 2 requires complete proposed inputs and expected Compose plus
node/native/OVS/QEMU artifacts for every scenario. It does not define the minimum
file set, whether expected files must be syntactically valid, whether abbreviated
sections are allowed, or how intentionally variable values are represented.
Without that contract, one response may provide executable-looking Compose while
another provides pseudocode, and both can claim completion.

Require a package manifest and minimum artifact rules. For example:

- Every scenario directory contains `graphx.yml`, `expected/inventory.json`, and
  `README.md`.
- The inventory declares target, placement, generated service/process counts,
  expected files, and whether each file is complete YAML/JSON, a command plan, or
  deliberately non-executable pseudocode.
- Shared type definitions live in one named catalog and scenario files reference
  them without copying them.
- Expected YAML and JSON must parse; placeholders use one documented syntax and
  must not contain secret values.
- Unsupported target/scenario combinations contain a machine-readable expected
  diagnostic rather than fabricated artifacts.

### F-03: Required traceability is stated only as an end condition

Severity: medium

The completion criteria require every accepted decision to map to a contract and
verification requirement, but none of the deliverables explicitly requires that
mapping. A long narrative can satisfy each topic while making omissions hard to
detect.

Require two concise matrices in the main report:

1. I-01 through I-11 mapped to proposed contract location, fixture(s), and
   planned verification.
2. Every required scenario, feature variant, and negative case mapped to its
   input, expected artifacts or rejection, execution target, and acceptance
   evidence.

These matrices should index the concrete package, not replace it.

### F-04: Platform coverage can be misread as universal support

Severity: medium

The prompt asks for native Linux, native macOS, OrbStack, and Lima placement for
every scenario. Current behavior intentionally does not support every scenario
in every environment. For example, privileged OVS work does not run natively on
macOS, OrbStack does not establish OVS acceptance, and current QEMU execution is
TCG-based. The prompt elsewhere asks for exact rejection of unsupported
configurations, but the per-scenario wording can still encourage invented
placements.

Change “identify ... placement” to require one of three values for every
environment: `supported placement`, `not applicable`, or `unsupported`, with the
capability rule and expected diagnostic for unsupported cases. State explicitly
that KVM may be a capability proposal or planned test dimension, but must not be
reported as current behavior or current evidence.

### F-05: New-issue classification is underspecified

Severity: medium

The prompt says not to reopen I-01 through I-11 and to number newly discovered
issues from I-12, but does not say what qualifies as an issue. Minor design
choices could be inflated into unresolved issues, while genuine contradictions
could be buried as implementation risks.

Define a new issue as a contradiction or missing requirement that prevents a
single consistent contract or acceptance test. Require each new issue to include
severity, affected scenarios, evidence, recommended resolution, and whether it
blocks implementation. Ordinary implementation risks and chosen concrete values
should remain in their respective sections without I-numbers.

### F-06: The output location and top-level package structure are optional

Severity: medium

The prompt suggests `design/graph-generation/` but does not require a report
filename or stable package shape. That weakens links, automated inventory checks,
and comparison between reviews. The directory does not currently exist, so no
repository convention resolves the ambiguity.

Specify a required root and report, such as:

```text
design/graph-generation/
  README.md
  architecture.md
  catalog/
  scenarios/
  negative/
  verification.md
  implementation-plan.md
```

Also require every file in that tree to carry a visible design-only status in
its header or containing README.

### F-07: Reproducibility inputs need a canonical representation

Severity: medium

The required architecture correctly asks for deterministic compilation, relative
path resolution, template/image version selection, and output ownership. It does
not require canonical ordering, stable serialization, path-containment rules, or
a definition of which environmental values may remain unresolved. Those details
are necessary to design a meaningful deterministic-generation test.

Require the proposed contract to define:

- canonical node, edge, service, and artifact ordering;
- normalized path base and rejection of paths escaping allowed roots;
- pinned type-catalog and image/build-recipe identities;
- treatment of host ports, addresses, timestamps, random identifiers, and
  credential references;
- byte-for-byte versus semantic determinism expectations; and
- overwrite/stale-output behavior for an existing generated directory.

### F-08: The execution boundary needs an explicit artifact-to-owner table

Severity: medium

The prompt appropriately rejects a new daemon and requires reuse of current
identity-checked operations. However, “the smallest execution glue” is open to
interpretation. A reviewer could accidentally create a generic orchestrator in
all but name, especially when combining Compose, native processes, OVS, and QEMU.

Require a table for each artifact/action identifying its owner, invocation
boundary, readiness input, failure result, cleanup owner, and persisted state.
State that the compiler may emit plans but may not execute them, and require the
review to identify the exact existing launcher or infrastructure API that would
consume each plan, or the smallest narrowly scoped adapter that is missing.

### F-09: Verification language should distinguish design checks from runtime checks earlier

Severity: low

Deliverable 4 makes this distinction well, and the authorization section forbids
privileged execution. The concrete-package section nevertheless asks for
“proposed acceptance evidence” before defining a standard vocabulary. Responses
may blur a parsed illustrative fixture, a planned test, and an actually executed
current-system test.

Require every check to be labeled `run-current`, `static-design`,
`planned-portable`, `planned-docker`, `planned-privileged`, or
`planned-guest-boot`. This directly reflects the evidence boundaries in
[`docs/test-procedure.md`](docs/test-procedure.md).

### F-10: Scope is large but internally coherent

Severity: observation

The prompt asks for a schema, compiler contract, catalog, target matrix, 15 full
scenario packages, six variants, multi-instance demonstrations, negative cases,
an implementation sequence, and a verification strategy. This is much larger
than a normal architecture review, but the breadth follows from the stated goal:
the design must preserve all current examples before production implementation.
Reducing the scenario set would weaken the most valuable part of the prompt.

Keep the scope, but require an inventory and traceability matrices as described
above. Also clarify that repeated generated boilerplate may be represented by a
fully defined shared generation rule plus complete per-scenario deltas only if
the resulting expected file can be reconstructed unambiguously. Otherwise,
require the complete file.

## What the prompt does well

- It separates current evidence from proposed future behavior and explicitly
  forbids production implementation and privileged mutation.
- It prevents the most likely architectural regressions: a second manifest,
  sample-specific templates, per-topology images, arbitrary Compose overrides,
  and a new lifecycle daemon.
- It recognizes that Compose, native processes, OVS, QEMU, and external devices
  have different execution and ownership contracts.
- It requires exact negative behavior for ambiguity, cardinality, locality,
  naming, missing artifacts, and target capability failures.
- It uses renamed and repeated node instances as a strong test of whether types
  are genuinely reusable.
- It preserves current semantic distinctions such as native shared memory,
  loopback multicast, OVS-backed MACVLAN/IPVLAN profiles, external guest
  ownership, deferred scenario actions, bounded history/capture, and separate
  simulator credentials.
- It correctly distinguishes TAP lifecycle evidence from guest boot evidence and
  asks the reviewer not to overclaim privileged or accelerator coverage.

## Recommended prompt edits

Before commissioning the review, make these changes in order:

1. Add the future-state precedence paragraph from F-01 under “Authority and
   scope.”
2. Make `design/graph-generation/` and its top-level file inventory mandatory.
3. Define the minimum complete scenario artifact set and parseability rules.
4. Require decision and scenario traceability matrices.
5. Require explicit supported/not-applicable/unsupported platform cells and
   clarify current TCG versus proposed KVM handling.
6. Define new-issue qualification and the evidence-status vocabulary.
7. Add canonicalization, path, output replacement, and unresolved-value rules to
   the deterministic compiler requirements.
8. Require the execution artifact-to-owner table to enforce the no-new-daemon
   boundary.

With these revisions, the prompt would be precise enough for two independent
reviewers to produce materially comparable packages and for a later
implementation phase to turn the selected design into focused tests without
having to reinterpret its contracts.
