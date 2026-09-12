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

- Configuration `version: 2` is the only accepted GraphX format.
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
