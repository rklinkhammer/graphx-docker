# Changelog

All notable GraphX changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and releases use
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- IPv4 UDP edges for unicast, isolated directed broadcast, and multicast, with
  bounded datagrams, cancellation, sequence-anomaly metrics, Wireshark support,
  and deterministic runnable examples.

### Fixed

- Made concurrent UDP close retain socket and cancellation descriptors until
  active operations finish, preflighted configured datagram limits before
  serialization, bounded repeated socket-error diagnostics, and added an
  offline broadcast runner plus a native Linux namespace acceptance lab with
  optional capability-gated live capture and TShark decoding.
- Preserved finite UDP receive deadlines under continuous malformed traffic and
  isolated portable acceptance stages so shutdown-only environment values do
  not leak into the UDP examples.

## [1.0.0] - 2026-09-04

### Added

- One authoritative `VERSION`, generated C++ version API, and `graphx --version`.
- Relocatable CMake install/export metadata and native TGZ packages for supported
  Linux and macOS hosts.
- Deterministic release manifests, SHA-256 checksum sets, SPDX 2.3 SBOMs, and
  archive/consumer verification.
- Tag-gated, digest-pinned release automation with provenance attestations and
  exact-version OCI image tags.
- Compatibility, release, upgrade, support, contribution, and security policy.

### Fixed

- Applied optional organization CA and reviewed installer trust consistently to
  verifier, runtime, telemetry, OVS, Compose, and native-network image builds.
- Hardened the Linux verification workflow for native build discovery, TLS and
  Wireshark checks, package installation, sanitizer diagnostics, and fuzzing.
- Made privileged macvlan and ipvlan lab cleanup resilient after interrupted or
  partially completed runs.
