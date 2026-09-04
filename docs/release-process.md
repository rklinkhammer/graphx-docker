# Release process

The release workflow is intentionally separate from ordinary CI. Local commands
build and verify candidates but never publish. Only an exact version tag starts
publication, and the protected `release` GitHub environment is the final approval
boundary.

## Maintainer preparation

1. Start from a clean, reviewed `main` commit. Confirm CI and all capability-gated
   tests applicable to the release have passed.
2. Choose the version under `docs/compatibility-policy.md`. Update `VERSION`, both
   `package.json` files and their lockfile root versions together.
3. Move `CHANGELOG.md` entries from Unreleased into a dated version section.
4. Run `python3 scripts/release/validate_version.py --tag vX.Y.Z`.
5. Perform a no-publish candidate build in new empty directories:

   ```sh
   python3 scripts/release/build_release.py \
     --build-dir build/release-local \
     --output-dir outputs/release-local \
     --tag vX.Y.Z
   python3 scripts/release/verify_release.py outputs/release-local --source .
   ```

   `--allow-dirty` exists only for development/testing and is prohibited for a
   release decision. `SOURCE_DATE_EPOCH` defaults to the commit time. Locale and
   timezone are fixed, static-archive timestamps are normalized, and metadata is
   deterministically ordered. Two builds using the same source, platform, and
   toolchain must be byte-identical. Cross-platform or cross-toolchain outputs
   are distinct artifacts and are not expected to match.
6. Inspect the manifest, SPDX 2.3 SBOM, checksums, licenses, archive contents, and
   downstream CMake consumer test. Sign/protect the exact `vX.Y.Z` tag according
   to repository policy and push it only after approval.

## Automated publication

The tag workflow first fails closed if the GitHub release or either final GHCR
version tag already exists. It then revalidates identity, portable acceptance,
pinned quality gates, fuzz smoke, npm production audits, native Linux/macOS
packages, package consumers, checksums, artifact-scoped native SBOMs, and archive
paths. Candidate verification receives trusted tag, commit, platform, and source
epoch values instead of trusting self-described metadata.

The native archive verifier enforces one canonical platform root and the complete
application, library, header, CMake, configuration, extcap, dissector, license,
and documentation layout shared with the builder. The explicit inventory must
equal the installed regular-file set: missing and unexpected files both fail.
Applications and extcap have portable mode `0755`; every other installed regular
file has mode `0644`. A mode mismatch fails verification. On macOS, CPack's
staged tree is re-archived without AppleDouble extended-attribute entries before
the candidate is accepted. Before hashing or opening an archive the verifier
enforces a 512 MiB compressed-file limit. Tar inspection is a single bounded pass
with limits of 10,000 members, 256 MiB per regular member, and 512 MiB cumulative
expanded regular-file content; links, special members, duplicates, traversal,
and file/descendant collisions are rejected.

Both digest-pinned-base amd64/arm64 images are initially published under a unique
`staging-RUN_ID-RUN_ATTEMPT` identity. BuildKit SBOM attestations and GitHub
provenance are attached to each immutable digest. After both staged images and
attestations succeed, a second absence check prevents a concurrent or retried
workflow from replacing a final tag. The job promotes both digests to exact
numeric version tags and confirms each digest. If either promotion fails, it
removes any final identity already promoted by that job. Only after this succeeds
does the workflow attest native archives and create the GitHub release, which is
the completeness marker for the two-image release set.

GitHub administrators must configure the `release` environment with required
reviewers and restrict tag creation. Workflow actions are pinned by commit. The
repository does not store registry credentials; GitHub's short-lived token and
OIDC identity supply scoped publication authority. The workflow does not create
`latest` tags. The repository must retain admin access to both workflow-published
GHCR packages so the compensating delete can run. GitHub currently describes
workflow package deletion through the REST API as public preview; a cleanup
failure is a hard stop requiring maintainer intervention, never permission to
overwrite the version.

Consumers with a trusted checkout verify a downloaded directory using `--source
.`. Offline consumers instead provide all four `--expected-tag`,
`--expected-commit`, `--expected-platform`, and `--expected-epoch` values from a
trusted release/attestation channel. A checksum-only integrity check may use
`sha256sum -c graphx-X.Y.Z-PLATFORM.sha256`, but it does not establish release
identity by itself. Consumers should also verify the GitHub
attestation and OCI digest using their organization-approved tooling. Extract an
archive into a new prefix, run `bin/graphx --version`, then exercise a canary
configuration before replacing an existing deployment.

## Failed release and rollback

Do not rerun a completed version, and never replace a final tag or checksum. The
workflow's compensating job removes exact OCI tags whenever GitHub release
publication does not succeed, including failure, timeout, skipped, environment
rejection, and cancellation outcomes. Cleanup deliberately has no second
environment approval that could strand promoted identities. A platform-level
force-cancel can prevent all jobs from running; after any such cancellation,
maintainers must check the GitHub release and both numeric GHCR tags and remove
only incomplete version identities before retrying. If cleanup itself fails,
maintainers must remove only the incomplete version identities and confirm
absence before any retry. Unique staging identities
are retained as audit evidence and are not release-completeness markers. Once a
GitHub release exists, stop rollout, mark it withdrawn, retain evidence for
consumers, and publish a corrected higher version. Roll back by
redeploying the previously recorded archive checksum or OCI digest and restoring
compatible configuration/data as described in `docs/upgrade.md`.

The workflow and CPack behavior follow the official
[GitHub artifact attestation guidance](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds),
[GitHub Docker publishing guidance](https://docs.github.com/en/actions/use-cases-and-examples/publishing-packages/publishing-docker-images), and
[GitHub package deletion guidance](https://docs.github.com/en/packages/learn-github-packages/deleting-and-restoring-a-package),
[Docker SBOM attestation guidance](https://docs.docker.com/build/metadata/attestations/sbom/), and
[CMake packaging documentation](https://cmake.org/cmake/help/latest/module/CPack.html).
The generated inventory uses the [SPDX 2.3 specification](https://spdx.github.io/spdx-spec/v2.3/).
