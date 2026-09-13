# Graph generation design review

**DESIGN ONLY — all files below this directory, including JSON, YAML, INI and
expected outputs, are non-runtime review artifacts.** I-01 through I-11 are
accepted future-state premises. P1 now implements authored version 3 and
normalized contract version 2, and P2 implements generic bindings and native
application verification; see [P2 verification](p2-verification.md). The
expected compiler artifacts in this package remain illustrative until P3; later
execution phases are not implemented.

The design meets the simplification objective if the binding and execution
boundaries in [architecture.md](architecture.md) land together. A topology change
then edits one graph and ordinarily creates zero Dockerfiles. Review the complete
example package before starting [the implementation phases](implementation-plan.md).
There are no newly discovered blocking issues. Remaining engineering risks and
selected restrictions are documented in the architecture, not reopened decisions.

## Reading the package

- [Architecture, contracts, execution ownership and traceability](architecture.md)
- [Reusable catalog and contract details](catalog/README.md)
- [Machine-readable case inventory](inventory.json)
- [Verification commands, actual results and planned acceptance](verification.md)
- [Phased implementation plan](implementation-plan.md)

The inventory contains 15 scenarios (`S01`–`S15`), six feature variants
(`V01`–`V06`), three additional topologies (`T01`–`T03`) and 15 negative inputs
(`N01`–`N15`). Each positive directory has a full authored graph and complete
illustrative expected files for each supported target. Native-only sets have no
Compose file because they have no containers: both applications and platform run
as native processes. That absence is intentional and recorded in their counts.
Unsupported target sets contain a diagnostic instead of runtime output.

## Inventory contract

`inventory.json.version` is 1. `design_only` is true. `cases` is sorted by stable
ID. Each entry contains `id`, `kind` (`scenario`, `variant`, `topology`, or
`negative`), `path` (package-relative directory), `input` (package-relative file),
and `status`. Positive entries name `expected_inventory`; negative entries name
`diagnostic` and `target`. `shared_files` indexes every non-case file, including
this README; it lists its path and format. Paths use `/`, never absolute paths or
symlinks. Inventories are review indexes, not compiler input or runtime ledgers.

Per-case `expected/inventory.json` has `case_id`, `status`, `artifact_sets`, and
`capabilities`. Each artifact set identifies one target, application/platform
placement, counts, `completeness: complete-illustrative`, and every expected file
as `{path, format, completeness, status}` relative to the case directory.
Diagnostic sets are represented by the capability's `diagnostic` field. The
inventory itself is indexed at the package level, avoiding a self-hash cycle.
Counts distinguish application/platform/auxiliary services, native application
and platform processes, QEMU, capture writers, the single capture handoff worker,
optional scenario processes and transient adapter/build processes. Service counts
count application processes, not threads or temporary health probes. No init
sidecars are hidden in these counts. Router namespaces and TAPs are resources,
not persistent processes. The CLI adapter count is one while start/stop runs; it
exits after completion. Build counts describe separately requested jobs, not startup.

## Evidence and unresolved values

`static-design` means a proposed artifact was inspected, never that GraphX can
execute it. Other statuses are `run-current`, `planned-portable`,
`planned-docker`, `planned-privileged`, and `planned-guest-boot`; definitions and
results are in verification.md. Proposed runtime support is not `run-current`.
No privileged operations or production implementation are authorized here.

There is one placeholder syntax: `${GX_NAME}`. Each supported set contains a
complete typed substitution table in `substitutions.json`. Only GX_OUTPUT,
GX_STATE, GX_CREDENTIALS, GX_RELEASE and GX_OWNER are permitted. Directory values
must be absolute, under the selected execution roots, and free of symlink
escapes. The owner token is a non-secret identity allocated by the existing
ownership store. Review substitution values are inert examples, not existing
paths or credentials. Static validation substitutes strings, then parses; it
never creates roots or reads secrets. Container-internal paths remain literal;
host mount sources use placeholders. No numeric placeholder is used.

Future release/image/guest digest literals in `catalog/lock.json` and its indexed
files are **illustrative identities**, not claims of published or built binaries.
They are deliberately syntactically complete digest pins, not `latest` tags.
Prometheus/Grafana pins are copied from current repository evidence. At
implementation, producing the actual release lock is a pre-compilation gate;
image or source identities may not remain unresolved at execution. Guest recipe
outputs are review expectations; no guest binary is shipped here. Missing
recipe/artifact declarations are compile errors, while missing built output
from a declared recipe is an execution prerequisite error. This distinction
allows a complete review of the build and boot plans without inventing a guest.

Not implemented: the v3 C++ model/compiler, generic sample/SDR bindings, portable
native platform packaging, release barriers, secure default telemetry delivery,
bounded adapters, generated observability integration and configurable guests.
Expected output was authored for review. No fixture generation framework or
production compiler is included. `check_package.py` only checks this static
package. Parseability, shape validation and reference checks do not prove
rendering determinism, runtime compatibility, isolation, cleanup or guest boot.
