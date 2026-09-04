# ADR 0011: Immutable release artifacts and explicit compatibility

Status: accepted

## Context

GraphX had phase-specific validation but no single product version, installable
consumer contract, immutable artifact metadata, or declared support boundary.
Independent version strings and mutable container tags could make a binary's
source and compatibility ambiguous.

## Decision

Use root `VERSION` as the authoritative stable numeric version and reject a tag
or JavaScript package/lock version that differs. Generate the C++ version API and
CMake package version from it. Produce platform-native TGZ archives with a
relocatable CMake export, exact-version CLI, licenses, schemas, extcap, and Lua
dissector. Verify installation and a downstream consumer before packaging.

For each archive, create a bounded JSON manifest, SHA-256 checksum set, and
artifact-scoped SPDX 2.3 native dependency inventory with a digest-specific
namespace. Bind offline verification to trusted tag, commit, platform, and source
epoch values. Reject unsafe, duplicate, ambiguous, linked, or special archive
paths; malformed metadata; unexpected files; dirty worktrees; noncanonical
versions; and reused output directories. Enforce the same canonical archive
layout in builder and verifier, and bound compressed size, member count,
individual expanded members, and cumulative expanded content before expensive
processing. Build native archives deterministically
within one platform/toolchain by fixing nested static-archive timestamps.

Release OCI images from digest-pinned bases, attach digest-bound BuildKit SBOM and
GitHub provenance attestations, stage both by a unique workflow-run identity, and
promote exact version tags only after both builds succeed and a fail-closed
existence check passes. Never publish an implicit `latest` alias. If later GitHub
release publication has any non-success result, remove both promoted version
identities without requiring a second environment approval; a version is
complete only when its GitHub release exists. Pin third-party workflow actions
and use GitHub environment approval, scoped tokens, OIDC provenance, and native
artifact attestation.

Treat wire, configuration, control/API, capture, persistence, C++ package, and OCI
identity as separate compatibility surfaces. Keep the existing v1/v2 envelope
and version-1 configuration semantics unchanged. Before 1.0, do not promise C++
ABI stability; require documented migration for breaking contract changes.

## Consequences

- Release publication is deliberate and can be reproduced locally without publish
  authority, while the final workflow remains tag- and approval-gated.
- Published versions and assets cannot be repaired in place; fixes require a new
  version and rollback uses a previously verified checksum or digest.
- Native SBOM content describes GraphX, its shipped pinned yaml-cpp library, and
  the exact externally linked OpenSSL version selected by CMake.
  OCI SBOM attestations separately inventory runtime, telemetry/npm, and base-image
  content without assigning those dependencies to the native archive.
- Platform packages are supported only where the release matrix tests them.
