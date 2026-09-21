# GraphX workspace rules

These rules apply to the repository. Read `docs/project-decisions.md` before
designing, implementing, reviewing, or testing a change.

## Working map

- `include/graphx/` defines public C++ contracts, `src/` owns reusable behavior,
  and `apps/` should remain thin executable entry points.
- The C++ configuration loader is authoritative. Keep its normalized JSON
  contract, schemas under `config/schema/`, examples, tests, and current
  documentation aligned when configuration behavior changes.
- Use `tests/` for focused C++ and Python coverage. Start from the nearest
  existing test and preserve its fixture and naming conventions.
- Treat `build/`, `outputs/`, and `captures/` as generated build, verification,
  or runtime evidence. Do not hand-edit them or use them as maintained source.
- Follow the topic guides under `docs/`; use
  [`docs/test-procedure.md`](docs/test-procedure.md) for environment selection and
  [`examples/README.md`](examples/README.md) for the complete example matrix.

## Architecture invariants

- Authored configuration `version: 3` is the only accepted GraphX format; normalized JSON uses contract version 2.
- System Open vSwitch on Linux is the only managed network backend. MACVLAN and
  IPVLAN names are semantic profiles, not Docker network drivers.
- Managed containers attach with identity-owned veth pairs. QEMU uses an
  identity-owned TAP attached to OVS.
- Docker Compose manages processes and management connectivity, not GraphX data
  plane networks.
- On macOS, OrbStack runs unprivileged Compose workloads and the dedicated Lima VM
  runs OVS, veth/TAP, namespaces, nftables, netem, capture, and QEMU laboratories.
- Privileged sockets are not forwarded to macOS. Runtime state and high-I/O
  artifacts stay on the Linux guest under `/var/lib/graphx`.
- Infrastructure mutation and cleanup are identity checked and fail closed.

## Change and verification rules

- Extend the authoritative configuration model and common ownership lifecycle;
  do not add parallel manifests or platform-specific launchers.
- Add focused negative tests when changing malformed-input, identity, interruption,
  or resource-boundary behavior.
- Run the smallest relevant check first: `scripts/verify.sh quick` for the
  development CTest suite and `scripts/verify.sh quality` for C++ formatting and
  static analysis. Run `scripts/verify.sh portable` before completing a normal
  cross-platform change.
- Use `scripts/verify.sh full` only after the selected Docker engine is confirmed
  ready. It does not run every example, Lima verification, QEMU guest boot, or
  browser checks; select those explicitly from the test procedure and example
  matrix when the changed surface requires them.
- Run ShellCheck on touched shell scripts and report the exact command because
  sourced-file paths vary by launcher.
- Run privileged tests only on native Linux or in the GraphX Lima guest with
  explicit authorization. Never initiate a privileged run merely because the
  host is Linux. Report Linux, Lima, TCG, and KVM evidence separately, and do not
  claim guest execution from TAP lifecycle tests alone.
- Documentation, tests, examples, and code describe only the current system.
  Development history belongs in Git, not maintained project artifacts.

## Simplification agent workflow

Apply this workflow when asked to simplify or refactor the repository. It defines
how the active agent works; it does not automatically start a separate agent or
authorize unrelated changes. Use `docs/simplification-report.md` as a candidate
backlog, not as authority over current source or project decisions.

### Scope the change

- Verify that the proposed problem still exists in current source. Identify the
  authoritative implementation, affected callers, and existing coverage.
- Work on one bounded change per assignment. State the redundant logic or mixed
  responsibility to remove, the behavior to preserve, and the verification needed.
- Preserve public commands, build defaults, artifact and schema contracts, package
  behavior, authorization, and ownership guarantees unless the user explicitly
  requests a change to them. Separate behavior changes from structural refactoring.
- Establish missing characterization coverage before changing production behavior;
  reuse existing fixtures, compiled goldens, and failure-injection mechanisms.

### Implement conservatively

- Prefer removing redundant decisions and orchestration over adding abstractions.
  Share behavior only where semantics match; smaller files alone are not evidence
  of reduced complexity.
- Keep resource-specific identity checks and recovery semantics explicit. Revalidate
  mutable identities under the ownership lock and at the relevant mutation boundary.
  Preserve recovery of outstanding intents when a mutation succeeds before its
  stable identity is recorded; retain ambiguous state for explicit recovery.
- Avoid general frameworks, parallel configuration models, new dependencies, and
  unrelated cleanup unless required by the bounded task.

### Verify and report

- Follow the change and verification rules above. Compare generated artifacts
  byte-for-byte for structural compiler changes and preserve the complete public
  topology contract when consolidating telemetry representations.
- For lifecycle changes, check interruption, partial completion, identity mismatch,
  and recovery behavior. Existing privileged-test authorization requirements apply;
  do not claim platform or guest evidence from unexecuted checks.
- Report what complexity disappeared, what interfaces or abstractions were added,
  which checks passed, and what remains unverified. Judge success by reduced
  maintenance burden and demonstrated behavioral equivalence, not line count.
- Update affected documentation and the simplification report to describe the
  resulting current implementation and remaining work.
