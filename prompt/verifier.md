# GraphX independent verification work package

Independently verify **Migration M8: compatibility and release closure** in
`~/workspace/graphx-docker`. Derive M8-001 through M8-010 from
`prompt/implement.md`, ADRs 0016-0018, and the accepted M7 report. Treat
`migration_m8_handoff.md` as a claim and write `migration_m8_verification.md`
without product fixes.

## Required checks

1. Build fresh and run quick, quality, sanitizer, portable, fingerprint,
   projection, schema, migration, documentation, M2-M7, telemetry, and web
   gates.
2. Prove version 1 remains strictly parseable, inspectable, projectable, and
   deterministically migratable, while all version-1 infrastructure actions
   fail before any external command or state mutation.
3. Prove production code contains no Docker-network create/remove/inspect
   realization and no imperative legacy netem planner.
4. Inspect every canonical migrated laboratory: `graphx.yaml` must be version
   2, plans must use the owned OVS lifecycle, and Compose must contain no
   macvlan/ipvlan/external GraphX data network.
5. Prove representative v1 sources exist only as compatibility fixtures and no
   active launcher or documentation instructs operators to realize them.
6. Prove the Docker Desktop userspace-OVS simulation and its privileged active
   launchers/manifests are absent.
7. Prove QEMU's default entry point selects TAP/OVS and never slirp; verify any
   retained external/container usernet path is explicitly deprecated and
   cannot be selected implicitly.
8. In Lima ARM64 Linux, run canonical M3-M7 regressions and the M8 closure test
   twice, including rollback/recovery and packet evidence. Do not claim native
   x86_64 or KVM results from TCG.
9. Verify upgrade, compatibility-window, rollback, storage, and complete
   platform release-matrix documentation.
10. Audit Docker networks, containers, OVS objects, veth/TAP devices,
    namespaces, qdiscs, ledgers, captures, timers, and QEMU processes for exact
    cleanup and unrelated-resource preservation.

M8 passes only when version 2 is the sole active network realization, legacy
inputs are migration-only, canonical OVS/TAP paths work on real Linux, and the
available platform evidence is reported without simulation claims.
