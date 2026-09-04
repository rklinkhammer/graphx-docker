# Security policy

## Supported versions

Before the first public tag, only the current `main` branch is eligible for a
security fix. After releases begin, the newest tagged version is supported.
Older pre-1.0 versions receive fixes only when a maintainer explicitly announces
an extended support window. See `SUPPORT.md` for the platform matrix.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use the repository's
private **Security > Report a vulnerability** form:
<https://github.com/rklinkhammer/graphx-docker/security/advisories/new>.
Include the affected version/commit, configuration, impact, reproduction steps,
and any suggested mitigation. Do not include production credentials or captures
containing sensitive payloads.

The project targets acknowledgement within three business days and an initial
assessment within seven business days. These are response objectives, not a
service-level guarantee. Disclosure timing and remediation depend on impact,
exploitability, and safe upgrade availability. Reporters will be credited when
requested and appropriate.

Released fixes use a new immutable version. Existing tags and assets are not
silently replaced. Operators should verify checksums and provenance as described
in `docs/release-process.md`.
