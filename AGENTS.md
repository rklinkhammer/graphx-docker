# GraphX workspace rules

These rules apply to the entire repository. Read
`docs/project-decisions.md` before designing, implementing, reviewing, or
testing a change.

## Normative sources

1. `AGENTS.md` and `docs/project-decisions.md` define the current project
   boundary.
2. Accepted records in `docs/adr/` explain durable architectural decisions.
3. Maintained feature documentation and executable tests define current
   behavior.
4. `docs/archive/` is historical evidence only. Never treat an archived plan,
   prompt, handoff, failed verification, or superseded command as a current
   instruction.

If these sources disagree, stop and resolve the active documentation or add a
new ADR. Do not silently revive an archived design.

## Architecture invariants

- Version 2 is the only mutable GraphX infrastructure format. Version 1 may be
  validated, inspected, projected, normalized, and deterministically migrated, but every
  version-1 infrastructure action must fail before mutation.
- System Open vSwitch on Linux is the only GraphX-managed network backend.
  MACVLAN and IPVLAN names are semantic profiles, never Docker network drivers.
- Managed containers attach to OVS with identity-owned veth pairs. QEMU uses an
  identity-owned TAP attached to OVS. User-mode/slirp QEMU paths are deprecated,
  explicit compatibility profiles and must never become defaults.
- Docker Compose may manage processes and management connectivity; it must not
  create a GraphX data-plane macvlan/ipvlan network.
- On macOS, OrbStack runs ordinary unprivileged Compose demos and Docker
  acceptance. The dedicated Lima VM runs privileged OVS, veth/TAP, namespaces,
  nftables, netem, capture, and QEMU network laboratories. Do not use or
  document Docker Desktop as a supported GraphX runtime.
- Do not forward Docker, OVS, QMP, or other privileged Lima sockets to macOS.
  Runtime state, captures, ledgers, and high-I/O artifacts stay on the guest's
  native filesystem under `/var/lib/graphx`.
- Infrastructure mutation and cleanup are identity checked and fail closed.
  Never delete a colliding or replaced resource by name alone. Declarative
  capture and bounded faults remain part of the owned lifecycle.

## Change and verification rules

- Extend the authoritative version-2 model, common ownership lifecycle, and
  canonical `up.sh`/`status.sh`/`down.sh` launchers. Do not add parallel OVS
  manifests or platform-specific legacy launchers.
- Add strict negative tests for malformed input, identity drift, interrupted
  startup, bounded resources, and exact cleanup whenever those boundaries are
  affected.
- Run `scripts/verify.sh portable` for normal cross-platform changes. Run
  `scripts/verify.sh full` only after `docker context show`, `docker info`, and
  `docker compose version` prove the OrbStack engine is ready.
- Run privileged network tests only on native Linux or inside the GraphX Lima
  guest with explicit authorization. Report native Linux, Lima ARM64, TCG, and
  KVM as separate evidence; never infer one from another.
- Keep new implementation plans and verification reports out of the repository
  root. Durable decisions belong in ADRs; completed work records belong in the
  archive when retention is useful.
