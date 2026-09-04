# Compatibility and versioning policy

GraphX uses one canonical numeric `MAJOR.MINOR.PATCH` value from `VERSION`.
CMake, the generated C++ header, both JavaScript packages, release manifests,
OCI labels, and tag validation derive from or must equal that value. Release tags
are exactly `vMAJOR.MINOR.PATCH`; prerelease, build metadata, aliases, and a
mutable `latest` image tag are intentionally unsupported by the current process.

## Compatibility surfaces

| Surface | Version boundary | Compatibility rule |
|---|---|---|
| GraphX envelope | Wire versions 1 and 2 | Readers retain documented v1/v2 behavior; a new incompatible encoding needs a new wire version. |
| YAML configuration | `version: 1` and JSON Schema | Additive optional fields may be minor changes; changed meaning, removal, or new required fields need a new config version and migration. |
| Control/telemetry messages | Explicit protocol fields and bounded API contracts | Additions must be safely ignored or negotiated; authorization or acknowledgement semantic changes are breaking. |
| PCAPNG application capture | PCAPNG 1.0, USER0, embedded GraphX wire version | Existing captures remain readable; incompatible payload changes follow the wire-version rule. |
| History database | Internal schema version/migrations | Upgrades must preserve supported data or fail before mutation; downgrades use a backup, not reverse migration. |
| C++ headers/library | Product version and CMake package version | Source compatibility is evaluated per release. No stable C++ ABI is promised before 1.0. |
| Telemetry HTTP/WebSocket API | Documented endpoints and response schemas | Additive response fields are compatible; removal/type/authorization changes are breaking. |
| Native archive/OCI image | Product version plus platform/digest | Final version identities are non-replaceable. Consumers verify the native trusted-identity/checksum contract and pin OCI digests. A GitHub release marks the two-image version set complete. |

Semantic version meaning before 1.0 is conservative: PATCH fixes defects without
intentional contract changes; MINOR may add functionality and may contain a
documented breaking change; MAJOR is reserved for a stable contract reset. Every
breaking pre-1.0 change must be called out in the changelog and upgrade guide.

## Deprecation and migration

Deprecations identify the affected surface, replacement, first deprecated
version, and planned removal version. When practical, retain the old behavior for
at least one subsequent minor release. Security fixes may shorten this period.
Readers should be introduced before writers emit a new wire/config representation.

Release rollback never overwrites an artifact or moves a completed tag. Restore configuration
and durable data from a pre-upgrade backup, deploy the previously verified digest,
and validate the older binary against the restored configuration. If stored data
was migrated, restore the backup rather than assuming downgrade compatibility.
