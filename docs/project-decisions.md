# GraphX current project decisions

This is the concise current-state guide for maintainers and coding agents. It
defines the invariants that apply to new code, tests, examples, and operational
guidance. Historical reasoning and evidence remain in
[`archive/`](archive/README.md); accepted architectural rationale remains in
[`adr/`](adr/README.md).

## Documentation ownership

Each maintained document has one role. Link to the owner instead of copying its
procedures or rationale into another guide.

| Document | Owns | Does not own |
| --- | --- | --- |
| `project-decisions.md` | Current mandatory invariants | Historical rationale or command walkthroughs |
| `adr/` | Durable architectural rationale and consequences | Current operator procedures |
| `GraphX_Architecture.md` | Current component relationships and system limits | Migration chronology or verification commands |
| `configuration-v2.md` | Authoritative version-2 and normalized-contract reference | Compatibility policy rationale |
| `compatibility-policy.md` | Supported versions, deprecation, and breaking-change rules | Configuration field reference |
| `test-procedure.md` | Automated verification profiles and prerequisites | Interactive demonstrations |
| `manual-test-procedures.md` | Platform-specific manual evidence and cleanup | Automated test catalog or demo tutorial |
| `demo-guide.md` | Runnable user workflows and expected outcomes | Release acceptance policy |
| `archive/` | Completed and superseded plans and evidence | Instructions for current work |

## Runtime boundaries

| Concern | Current decision |
| --- | --- |
| Configuration | Version 2 is authoritative for infrastructure mutation. Version 1 is migration input only. |
| Network backend | Linux system Open vSwitch is the sole GraphX-managed backend. |
| MACVLAN/IPVLAN | Semantic behavior profiles realized by OVS, routing, and policy—not Docker network drivers. |
| Containers | Docker manages process lifecycle and management connectivity; GraphX attaches verified containers to OVS using owned veth pairs. |
| QEMU | TAP attached to OVS is the default. Slirp/user networking is explicit, deprecated compatibility behavior. |
| Capture and faults | Declarative, bounded, identity-owned lifecycle objects. Imperative netem and untracked capture processes are not supported. |
| macOS containers | OrbStack provides Docker/Compose for unprivileged demos and the `full` Docker acceptance tier. |
| macOS privileged networking | The dedicated ARM64 Lima guest owns rootful Docker, system OVS, veth/TAP, namespaces, nftables, netem, dumpcap, and QEMU network state. |
| Persistent runtime data | Guest-native `/var/lib/graphx`; never a macOS shared mount. |

OrbStack and Lima have different jobs. OrbStack is not evidence for system OVS
or Linux host-network mutation. Lima does not export its Docker or privileged
sockets to macOS and therefore does not satisfy the host-side Docker portion of
`scripts/verify.sh full`.

## Compatibility boundary

- Version-1 configurations remain accepted only by `validate`, `inspect`,
  `project`, `config normalize`, and deterministic `config migrate` operations during the published
  support window.
- The C++ loader is the authoritative normalizer. `graphx config normalize`
  emits the independently versioned, strict JSON contract used by downstream
  consumers; version-1 output is always marked non-mutable. The source `version`
  is immutable and cannot be changed by `GRAPHX_OVERRIDES` or `--set`.
- `load_config()` remains the public configuration entry point. Shared YAML
  parsing lives behind it, while `src/config_v1_compat.cpp` quarantines legacy
  driver vocabulary and `src/config_v2.cpp` owns authoritative semantic-profile
  interpretation. Migration remains exclusively in `src/migration.cpp`.
- Every version-1 `infra` action refuses before creating a state directory or
  mutating Docker/Linux resources.
- Curated version-1 infrastructure-migration fixtures live under
  `examples/compatibility/v1`. Some transport-only examples remain version 1
  during the published compatibility window, but they are never inputs to
  infrastructure execution.
- Canonical infrastructure examples use version 2 and ordinary `up.sh`,
  `status.sh`, and `down.sh` launchers. Parallel OVS manifests, platform-split
  launchers, and the Docker userspace-OVS simulator must not return. The single
  `scripts/network-lab.sh` entry point runs those launchers locally on Linux or
  dispatches them into the identity-checked Lima guest on Apple Silicon macOS.
  Shared platform checks, port validation, and lifecycle sequencing belong in
  `scripts/lib/demo-runtime.sh`, not in per-example wrappers.

## Ownership and safety boundary

- Creation records stable owner, graph, configuration, and kernel/OVS object
  identity before later status, destroy, or recovery operations trust it.
- Replacement or ambiguous resources fail closed. Matching a name is never
  sufficient authority to alter or remove an object.
- Startup is transactional and rolls back only objects proven to have been
  created by that attempt.
- Cleanup preserves unrelated Docker networks, containers, OVS objects,
  namespaces, links, qdiscs, processes, and files.
- Zero-byte mode-0600 per-graph lock files are persistent synchronization
  metadata, not live-run ownership ledgers.

## Verification boundary

| Evidence | Supported execution boundary |
| --- | --- |
| Quick/quality/sanitizer/fuzz/portable | macOS or Linux with the documented toolchain |
| Full Compose acceptance on macOS | OrbStack context with a reachable engine and Compose |
| Privileged OVS/veth/TAP lifecycle | Native Linux or the dedicated Lima Linux guest |
| Native Linux certification | Dedicated native Linux host only |
| x86_64 KVM | Native Linux x86_64 with accessible KVM only |
| x86_64 TCG on Apple Silicon | Lima ARM64; record as TCG, never KVM |

Before the lengthy `full` profile, require all of these to succeed:

```sh
orb status
docker context show
docker info
docker compose version
```

The selected context must be `orbstack` for the supported macOS workflow.
Privileged Lima checks are a separate evidence row and follow
[`manual-test-procedures.md`](manual-test-procedures.md).

## Durable decision sources

- ADR 0016: Lima owns the macOS privileged OVS runtime.
- ADR 0017: OVS is the single backend; containers use veth and QEMU uses TAP.
- ADR 0018: version 2 is the sole active realization and legacy paths are
  migration-only.
- ADR 0019: the C++ loader owns the deterministic normalized JSON contract.
- Earlier accepted ADRs continue to govern configuration authority, bounded
  parsing/runtime behavior, transport compatibility, security, telemetry,
  history, control, capture, releases, UDP, QEMU, external devices, and manual
  route activation.

Changes that alter one of these decisions require an explicit ADR update and
matching implementation, negative tests, operator documentation, and
platform-accurate verification evidence.
