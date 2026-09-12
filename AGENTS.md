# GraphX workspace rules

These rules apply to the repository. Read `docs/project-decisions.md` before
designing, implementing, reviewing, or testing a change.

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
- Run `scripts/verify.sh portable` for normal cross-platform changes. Use `full`
  only after the selected Docker engine is confirmed ready.
- Run privileged tests only on native Linux or in the GraphX Lima guest with
  explicit authorization. Report Linux, Lima, TCG, and KVM evidence separately.
- Documentation, tests, examples, and code describe only the current system.
  Development history belongs in Git, not maintained project artifacts.
