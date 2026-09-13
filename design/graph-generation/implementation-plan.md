# Phased implementation plan

**Phase status:** P1 is complete as the authorized model cutover; see
[P1 verification](p1-verification.md). P2 is complete with generic bindings and native application verification; see
[P2 verification](p2-verification.md). P3–P10 remain future changes. The original
design review did not authorize implementation, guest builds, infrastructure
mutation or privileged tests. I-01–I-11 are accepted; no compatibility parser, second source manifest,
deployment daemon, generic plugin system or per-topology image build is planned.

| Phase | Concrete outcome and affected components | Smallest abstraction | Removal / atomic contract boundary | Verification and completion gate |
|---|---|---|---|---|
| P0 — design gate | Review this package's 24 positive cases, negative diagnostics, schema limits, owner table and actual static results | No production abstraction | No production files change | All required artifacts and both matrices present; static checks pass; reviewers can assess every target and limitation. This is the gate before P1 |
| P1 — one authored/resolved model | Extend `include/graphx/config.hpp`, `src/config_*.cpp`, `src/normalized_config.cpp`, `config/schema/`, telemetry normalized consumer/tests | Type instances, typed connections and pinned catalog in existing C++ loader | Replace v2 with v3 and normalized 1 with 2 together; reject v2 deliberately. Update all checked-in authored examples and current-system docs in the same cutover commit; temporary unconverted launchers must fail explicitly rather than parse two formats | Start with nearest config/normalized tests; test all positive inputs, N01–N15 and strict unknown/duplicate keys, bounds, schemes, no secret values. Complete closed nested normalized schema and all consumers before merging; `quick`, `quality`, `portable` |
| P2 — generic bindings | `include/graphx/node_settings.hpp`, `src/node_settings.cpp`, reusable sample logic in `src/`, thin sample/UDP apps; SDR Python common code | Required node identity and port settings; local-ready/release interface | Remove generator/samples/transformed/sink lookup defaults and sample telemetry IDs together with node-file reader; use no graph-specific branches | Native S01/S02/S03/S04/S06 semantic tests and T01 two independent pipelines; validate missing/wrong node, schema, max peers and bounded interruption; SDR source filters/control errors; `quick`, `quality`, `portable` |
| P3 — deterministic compilation | `include/graphx/compile.hpp`, `src/compile.cpp`, binding/artifact serializers, thin CLI `compile` | Pure resolved value and fixed serializers | Remove descriptive per-node image/command metadata after type resolution consumers migrate; no second interpreter | Golden expected files, shuffled map order and relocated roots, byte-repeat compilation, catalog/image/source pins, no runtime calls, path/symlink/output replacement refusal. Real release pins replace review identities. `quick`, `quality`, `portable` |
| P4 — shared packaging/templates | Runtime Dockerfile, telemetry Dockerfile, SDR shared service packaging, `config/catalog/`, release scripts/SBOM | Two fixed templates and reviewed software recipes | Remove baked sample graph and generator entrypoint, per-graph build sections, topology-specific image copies; type/image/template revisions land together | Reproducible release/image checks, SBOM/checksums, no secrets in layers; Compose config validation; zero Dockerfiles for S01/T01/T02. Live Docker only after engine prerequisites, explicitly recorded per engine |
| P5 — default platform and credentials | Existing telemetry/history/control modules, native Node/web release bundle, common credential staging | One platform config and narrow reference-to-file provisioning | Remove normalization-at-startup container and six source overlays only when replacement variants pass; preserve current authenticated authorization boundaries | S01/S11 zero-app platform, native graphs, V01–V06. Test DB+WAL bounds/queue drops/query timeout, restart persistence and explicit deletion; per-node credentials, wrong origins, stale/nonced requests, idempotency and audit. Native/portable tests; separately Linux Docker and OrbStack. Browser/manual console checks remain explicit |
| P6 — portable/native execution | CLI adapter, common `scripts/lib/demo-runtime.sh`, existing ownership store/process module | Fixed startup barrier and identity-checked process kind | Replace native shell PID loops and Compose sample-specific dependency wiring; remove hidden endpoint env defaults together with their launch consumers | S01–S06, S13, all variants, T01–T02. Native Linux/macOS and selected Docker engines separately. Busy ports, process identity substitution, interrupted readiness, bounded logs, exit/cleanup and preserved external workloads; `quick`, `quality`, `portable` |
| P7 — OVS realization from compiled graph | Existing lifecycle coordinator, endpoint/namespace/OVS modules, network dispatcher, external boundary helper, sealed handoff | Expand logical attachments/router interfaces; add management ACL to existing endpoint ownership | Remove fixed host veth names, legacy Docker-parent links, duplicate router attachments and per-profile wiring; current OVS semantic contracts and new resolved consumers change together | S07–S12 and S14 resource plans first; N01/N08/N11, ownership corruption and interruption negatives. Then separately authorized Linux and Lima profile acceptance, policy/route and management-bypass tests, bounded captures. Remove old wrappers only after each profile passes; ShellCheck exact touched-script commands |
| P8 — owned guest contract | Common QEMU process resources, existing guest build/manifest tools, guest echo code, new SDR guest build recipe, Lima access boundary | One guest artifact set plus bounded config/credential/readiness channels | Replace TAP launcher hard-coded guest/peer/address/QMP paths and per-launch PID ledger; external S11 remains external. Container image and guest artifacts remain separate | Missing artifact/checksum/architecture/channel identity tests first. S15 actual x86_64 TCG boot on Linux x86_64 and Lima ARM64; T03 must run actual SDR guest samples/control/results. Distinguish authorized TAP lifecycle from guest boot; KVM remains a future separate proposal/test dimension |
| P9 — scenario actions and final conversion | Existing route/fault/control operations, narrow scenario CLI, all example READMEs and acceptance scripts | Closed action enum over resolved graph resource references | Remove embedded startup fault timers, deferred-route shell duplication, credential-rotation overlays and external simulator ambiguity. Source graphs/actions replace source Compose/Dockerfiles only after corresponding feature gates | S11 timed fault clear, S12 route absent/apply/clear, S14 physical-vs-lab exclusivity and trust restoration, S15 verification traffic, V04 overlap/expiry. Verify no action runs at baseline and interruption leaves ownership recovery data |
| P10 — removal and release gate | `examples/README.md`, topic docs, all example acceptance, release checks and repository hygiene | No new abstraction | Delete superseded launchers/manifests/config copies, stale schema snapshots, copied template fragments and sample assumptions; no long-lived dual path | Complete every case from this package with evidence status upgraded only for actually run behavior. `portable`, applicable Docker/full checks after ready engine, all explicitly authorized Linux/Lima labs, TCG boot and browser checks. Current docs describe only implemented v3 |

P1 is an atomic model cutover, not a compatibility period. It may be developed in
an isolated branch with later phases, but must not land a half-readable repository.
Before P1 merges, authored examples, schema, normalized consumers and current
contract docs agree; launchers that require later execution adapters give a clear
unimplemented capability error. Prefer keeping the cutover branch unmerged until
P6 for portable users, and until P8/P9 where the repository's release gate requires
the full example matrix. Each phase is separately reviewable without promising
that intermediate commits are independently deployable releases.

Each phase starts with the smallest focused test; follow
[the current test procedure](../../docs/test-procedure.md). `quick` precedes
broader checks; run `quality` for C++ changes and `portable` for normal
cross-platform changes. `full` requires a confirmed selected engine and is not
proof of every example or QEMU boot. For touched shell files, record the exact
ShellCheck invocation and sourced-file search path. Privileged Linux/Lima runs
always require separate explicit authorization, regardless of host OS. Do not
start or replace a Lima VM, delete another workload, or forward a privileged
socket to make a test pass.

The final completion evidence must include pre/post owned resource inventories,
preserved existing workloads, retained sealed captures/logs, actual guest
architecture/accelerator, and distinct native Linux, native macOS, OrbStack and
Lima results. Proposed runtime features cannot inherit this design package's
`static-design` result as execution evidence.
