# Upgrade and rollback guide

1. Record the current `graphx --version`, native package checksum or OCI digest,
   configuration, credentials-file locations, and database/capture volumes.
2. Back up durable telemetry history and authoritative configuration. Captures are
   evidence, not a migration source. Keep secrets out of the backup manifest.
3. Read the target changelog and compatibility policy. Validate the candidate
   checksum/provenance and run `graphx validate` against a copy of configuration.
4. Install into a new prefix or deploy the exact new OCI digest. Do not overwrite
   the known-good artifact. Run health, readiness, traffic, telemetry, control,
   and capture canaries relevant to the deployment.
5. Promote gradually and monitor the Phase 6 SLOs and Phase 8 audit stream.

If validation fails, stop new traffic, drain where possible, deploy the previously
recorded artifact/digest, restore the saved configuration and any pre-migration
history backup, and rerun the same canaries. Do not point an older telemetry
runtime at a database written by a newer schema unless that downgrade is
explicitly documented. Preserve logs and manifests for diagnosis.

## Configuration version 1 to version 2

Version 1 remains accepted for validation, inspection, projection, normalization, and migration
through the next major release. M8 rejects all version-1 infrastructure
execution; the Docker bridge, macvlan, and ipvlan realization is retired. To
prepare a reviewed version-2 copy without changing the source:

```sh
graphx config migrate graphx.yaml --output graphx-v2.yaml
graphx validate graphx-v2.yaml
graphx inspect graphx-v2.yaml
```

The migration is deterministic, refuses an existing output or symlink, and
fails rather than guessing when legacy network intent is ambiguous. Compare the
semantic profiles and typed attachments with the source before adopting the new
file. Version-2 infrastructure uses the identity-safe OVS lifecycle for
container/namespace veth, QEMU TAP, routes and policy, bounded mirror capture,
and timed netem faults. Capture evidence is retained outside the ownership
ledger; archive or remove it under the deployment's evidence-retention policy.

Migration reads the literal source file and intentionally ignores
`GRAPHX_OVERRIDES`. Clear or retain runtime overrides as needed for normal
execution, but do not expect them to be encoded into a migrated document.

Keep the original version-1 file unchanged. Before rollout, rollback is simply
abandoning the candidate. After rollout, rollback requires the previously
recorded GraphX release plus the saved version-1 configuration; an M8 binary
will not recreate the retired Docker data plane. Do not hand-edit the version
number or translate profiles back into Docker drivers. See
[`m8-compatibility.md`](m8-compatibility.md).
