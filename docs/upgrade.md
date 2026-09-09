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

Version 1 remains accepted and keeps its Docker bridge, macvlan, and ipvlan
meaning. To prepare a reviewed version-2 copy without changing the source:

```sh
graphx config migrate graphx.yaml --output graphx-v2.yaml
graphx validate graphx-v2.yaml
graphx inspect graphx-v2.yaml
```

The migration is deterministic, refuses an existing output or symlink, and
fails rather than guessing when legacy network intent is ambiguous. Compare the
semantic profiles and typed attachments with the source before adopting the new
file. M2 models intent only: version-2 infrastructure, route, and fault commands
fail closed until the M3 OVS lifecycle exists.

Migration reads the literal source file and intentionally ignores
`GRAPHX_OVERRIDES`. Clear or retain runtime overrides as needed for normal
execution, but do not expect them to be encoded into a migrated document.

Rollback is a configuration selection, not a reverse rewrite. Keep the original
version-1 file unchanged and select it again if the version-2 review fails. Do
not hand-edit the version number or translate profiles back into Docker drivers.
