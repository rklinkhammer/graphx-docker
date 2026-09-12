# Release process

1. Update `VERSION` and the package versions in `apps/telemetry` and `web`.
2. Run `scripts/verify.sh portable`, then the platform-specific full and privileged
   checks required by the changed surfaces.
3. Build a clean release candidate:

```sh
python3 scripts/release/build_release.py \
  --build-dir build/release \
  --output-dir outputs/release \
  --tag "v$(tr -d '\n' < VERSION)"
```

4. Verify it independently:

```sh
python3 scripts/release/verify_release.py outputs/release --source .
```

The verifier checks archive layout and modes, native package contents, checksums,
image digests, the SPDX SBOM, configuration schemas, and installed consumer use.
Published artifacts must be built from a clean tree and use digest-pinned base
images. Keep signing credentials outside the repository.
