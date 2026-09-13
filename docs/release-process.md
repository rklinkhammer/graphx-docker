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

## Shared images and catalog pins

The runtime, telemetry and SDR images have one software recipe each:
`Dockerfile`, `docker/telemetry.Dockerfile` and `docker/sdr.Dockerfile`. They use
the two fixed catalog templates, `node-v1` and `platform-v1`. No graph generates a
Dockerfile or selects a build context. Runtime packaging has no sample graph or
generator entrypoint. The SDR image installs the three catalog executable names;
it contains application code, not the packet-observation tooling owned by the
infrastructure lifecycle. All three images run as UID/GID 65532.

After checking the selected Docker engine (`orbstack` on macOS), build an offline
candidate without publishing or creating a privileged builder:

```sh
python3 scripts/release/image_release.py build \
  --output outputs/shared-images --platform linux/arm64 --no-cache
python3 scripts/release/image_release.py verify outputs/shared-images
```

Use `linux/amd64` for an amd64 candidate. `--allow-dirty` is available for local
implementation verification and explicitly marks the image manifest as a dirty
candidate. Such output is not a publishable release. Every build runs twice;
`--no-cache` forces independent builds. Debian dependencies use the dated,
signature-verified snapshot in `docker/debian.sources`; npm dependencies use the
checked-in locks, and the SDR runtime comes from a digest-pinned Python base.

The builder verifies Docker's OCI-compatible save output, normalizes layer and
archive timestamps offline, and derives new OCI config and manifest digests from
the resulting bytes. It preserves layer order, ownership, modes, links and xattrs.
This accommodates Docker image stores that do not apply the BuildKit timestamp
export option. Both the complete archive bytes and image inventories must repeat.
The finished archives are imported and smoke-tested with no network, a read-only
root filesystem, no capabilities and the declared non-root user. Only temporary
image identities are cleaned up. No graph workload is launched.

`images.json` binds each archive checksum to its OCI manifest digest and actual
installed files. Each image includes a separate SPDX inventory derived from its
installed OS packages, language runtime and Node modules. The independent verifier
rehashes every layer and regenerates that inventory. It rejects embedded authored
graphs, credential directories and PEM private keys; exact published GnuTLS
self-test fixtures are recognized only inside `libgnutls`. This is a bounded
content check, not a claim to detect every possible encoding of a secret.

The generated `catalog/` is a release-specific copy of the authoritative catalog,
with image pins, container type revisions and lock hashes updated together.
Author a graph with a relative path to that lock and pass its directory as
`--catalog-root` when compiling. The source catalog remains an explicitly
unverified development input. Offline OCI digests do not imply registry
availability; publishing requires preserving or independently verifying the
registry manifest. The compiler still marks execution unavailable. P5 supplies
the resolved platform configuration adapter; native platform packaging and guest
execution remain their separate phase gates.

The release workflow builds, attests, validates SPDX inventories and promotes
all three shared registry images together. It refuses existing version tags and
includes SDR in compensating cleanup. Running the local commands above does not
invoke that publication workflow.
