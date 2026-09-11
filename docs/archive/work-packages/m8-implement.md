# GraphX implementation work package

Implement **Migration M8: compatibility and release closure** in
`~/workspace/graphx-docker` after the independently accepted M7 boundary.

## Objective

Make the version-2 OVS lifecycle the only active GraphX-managed network
realization. Preserve version-1 parsing and deterministic migration for the
published compatibility window, but prevent legacy files from mutating Docker
or Linux networking. Promote OVS/veth/TAP examples to their canonical entry
points and remove the Docker Desktop network simulation from active use.

## Required implementation

- Keep version-1 validate, inspect, project, and `config migrate` behavior, but
  fail every version-1 `infra` action closed with an actionable migration
  diagnostic. Never reinterpret or realize a version-1 Docker driver.
- Remove the Docker bridge/macvlan/ipvlan network planner and the imperative
  legacy netem command from the production infrastructure surface. Version 2
  continues to use the transactional ownership ledger, declarative faults, and
  manual declared-route transition.
- Make each migrated M5 laboratory's `graphx.yaml`, Compose file, and ordinary
  `up/status/down` launcher select the OVS system-datapath implementation.
  Docker Compose may supply process lifetime and management connectivity only.
- Retain representative version-1 configurations as clearly scoped,
  non-executable migration fixtures. Remove legacy Docker macvlan/ipvlan
  Compose manifests and the privileged Docker Desktop userspace-OVS simulator.
- Make the QEMU TAP/OVS profile the default documented and scripted path on
  Linux and macOS-through-Lima. Keep external/container slirp profiles only as
  explicitly deprecated compatibility entry points; no default may select
  `-netdev user` or Docker data-plane networking.
- Publish an M8 upgrade/compatibility guide with support window, migration,
  rollback, platform, and release-matrix requirements. Preserve M0
  fingerprints as historical records rather than executable legacy plans.
- Add portable closure tests that scan production and active examples for
  forbidden Docker-network realization, exercise v1 parse/migrate/refusal,
  verify canonical OVS/TAP defaults, and preserve M2-M7 contracts.
- Add a privileged Linux/Lima closure regression that proves canonical network
  labs still use only OVS/veth/TAP, complete two clean lifecycle cycles, and
  leave no Docker data networks or infrastructure residue.

## Verification

Run formatting, quick, quality, sanitizer, portable, schema, migration,
fingerprint, documentation, telemetry, web, and M2-M7 gates. Run the
authoritative Lima verifier, prior privileged regressions, the M8 closure
regression twice, and an exact Docker/OVS/link/namespace/qdisc/process residue
audit. Record platform-matrix evidence honestly; unavailable native x86_64 KVM
must not be inferred from Apple Silicon TCG.

Write `migration_m8_handoff.md`. Do not commit, publish, or begin post-migration
feature work.
