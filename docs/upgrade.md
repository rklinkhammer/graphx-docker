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
