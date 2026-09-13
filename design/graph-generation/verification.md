# Verification definition and results

Implementation evidence is recorded separately in [P1 verification](p1-verification.md),
[P2 verification](p2-verification.md) and [P3 verification](p3-verification.md).
The checks below concern illustrative design artifacts; they are not runtime acceptance.

**DESIGN ONLY.** Static validation does not demonstrate runtime support.

| Status | Meaning |
|---|---|
| `run-current` | Check actually executed against implemented behavior, with command/environment/result/evidence |
| `static-design` | Parse, shape, reference, inventory or consistency check of proposed artifacts; report whether run |
| `planned-portable` | Future C++/native/portable requirement; name targets |
| `planned-docker` | Future live container requirement; name selected engine |
| `planned-privileged` | Future separately authorized Linux or Lima infrastructure requirement |
| `planned-guest-boot` | Future actual guest execution with architecture and accelerator |

## Actually performed

Results and final counts are recorded in [static-results.json](evidence/static-results.json)
and [current-results.txt](evidence/current-results.txt). The static checker is
[check_package.py](check_package.py), a package-only read-only checker, not a
production compiler or fixture generator.

Environment: native macOS ARM64; Python 3.13 isolated environment at
`/tmp/graphx-design-validation-venv`, PyYAML 6.0.3 and jsonschema 4.26.0.
Docker Compose v5.1.2 performs configuration checks only, with no engine connection.
The C++ CLI validates authored v3 inputs. P1 portable checks select Node 24
explicitly; current command output is linked below.

| Status | Command / check | Environment and result | Evidence |
|---|---|---|---|
| `static-design` | `/tmp/graphx-design-validation-venv/bin/python design/graph-generation/check_package.py --compose` | macOS ARM64; final result recorded in JSON | [static results](evidence/static-results.json) |
| `run-current` | `python3 tests/test_example_configs.py build/dev/graphx .` | current C++ CLI validates all 24 authoritative v3 example configurations | [P1 results](p1-verification.md) |
| `run-current` | Node 24 `node --version`; `build/dev/graphx validate examples/sample-pipeline/graphx.yml` | Node v24.20.0; sample accepted as version 3 | [current results](evidence/current-results.txt) |

Static scope: strict duplicate-key YAML and JSON parsing (fixtures use the common
YAML 1.1/1.2 subset; general YAML 1.2 parser conformance is pending); all 39 case IDs; graph/type
JSON Schema structural checks for positives; normalized outer schema plus
node/resolved/platform equality; shared catalog hashes; per-type ports, schema,
encoding, parameter range and min/max peers; target set/diagnostic completeness;
all expected files and provenance hashes; process/service/capture/guest counts;
attachment and capture references; placeholder declarations and typed review
substitution; credential-reference inventory; relative Markdown links; both
traceability matrices. Each Compose file is substituted into a temporary file
and passed to `docker compose -f <temporary-file> config --quiet`. This does not
pull images, contact an engine, create secrets, or start containers.

## Pending and limitations

- `static-design`: the illustrative normalized schema checks outer shape and
  selected cross-field invariants. Production P1 has a fully closed nested schema
  validated by C++ and telemetry; its evidence is separate.
- `planned-portable`: artifact compilation, output publication/ownership,
  orchestrated startup and generic application execution require later phases.
  P1 executes N01–N15 against C++; this static checker only indexes those fixtures.
- `planned-docker`: image availability, real credential staging and orchestrated
  graph workloads have not run. Compose configuration validation contacts no engine.
- `planned-privileged`: no OVS, veth/TAP, namespace, nftables, netem, capture or Lima
  operation was executed. No privileged authorization is inferred.
- `planned-guest-boot`: no guest was built or booted. TCG/KVM guest execution is not
  claimed. KVM remains outside baseline support.
- Browser visual acceptance is not established by API tests or a web build.
  P1 standalone security/history/capture integration results do not establish
  graph-driven credential provisioning, platform deployment or guest behavior.

## Planned acceptance definitions

| Check | Status and targets | Acceptance |
|---|---|---|
| Version and schema cutover | P1 verified on native macOS; see [evidence](p1-verification.md) | v3 accepted, v2 rejected, all unknown/duplicate keys rejected; normalized contract 2 accepted by every consumer; complete nested schema |
| Deterministic compilation | `planned-portable`; Linux/macOS | same fixed inputs produce byte-identical trees twice; shuffled mapping insertion order does not change output; relocating roots changes only declared execution paths; no clock/random/ambient environment reads |
| Catalog and build pinning | `planned-portable`; Linux/macOS build tooling | changed catalog bytes without matching lock rejected; no `latest`, missing digest, secret build layer or source escape; topology edits generate zero Dockerfiles |
| Output ownership | `planned-portable`; Linux/macOS | empty output exclusive publish; same-owner explicit replacement; reject symlink, foreign/edited file, stale manifest, active consumer and interrupted staging; no unrelated file deletion |
| Endpoint/resource bounds | `planned-portable`; Linux/macOS | all N cases exact primary code/path; duplicate listener including wildcard conflicts; distinct TCP/UDP allowed; name hash collision test injection rejects; invalid multicast/interface/TTL/CIDR rejected |
| Renamed/multiple instances | `planned-portable`; Linux/macOS; `planned-docker` Linux Docker/OrbStack/Lima Docker | S01/T01 shared catalog hashes unchanged; every binding and trace identifies its real node/edge; both streams produce only their own results; T02 separate sinks accepted and N12 rejected |
| Native transport preservation | `planned-portable`; Linux x86_64/macOS ARM64, Lima guest-native separately | S02 segments/capacity/4096-byte rejection/backpressure/interruption; S03 127.0.0.1 UDP; S04 multicast loopback TTL 0; S06 bounded capture, sequence/value and sealed console download |
| Portable data networking | `planned-docker`; Linux Docker/OrbStack/Lima Docker separately | S01 and S05 message delivery; broadcast limited to declared subnet/interface; management listeners never used for application data; default four-service sample, three-service broadcast |
| Default platform/history | `planned-portable` native Linux/macOS; `planned-docker` all supported engines | zero-app graph still has platform; HMAC isolation; restart persistence; retention/record/disk+WAL/queue/query bounds; explicit delete refuses active or foreign store; non-secret health endpoint |
| Feature variants | `planned-docker`; Linux Docker/OrbStack/Lima Docker | V01 explicit retention; V02 real Prometheus scrape/Grafana provision and bounded stores; V03 authorized generator pause/resume with denied wrong node/origin/token/replay; V04 operator and runtime rotation overlap then expiry; V05 CA+bearer validation; V06 client cert/key, SAN/CA/expiry rejection |
| OVS semantics | `planned-privileged`; separately authorized Linux x86_64/Lima ARM64 | S07 unique MAC; S08 shared MAC across routed L2 domains, ordered allow/default-drop and mirrors; S09 destination-IP L3 unicast and no cross-domain broadcast; S10 mixed profile routing; remove data path and prove management cannot bypass it |
| Observation / scenarios | `planned-privileged`; Linux/Lima separately | S11 zero managed guest boots, bounded 4-file ring and timed fault cleanup; S12 allowed/denied/deferred route before/apply/clear; sealed handoff rejects symlink/partial/foreign captures; external health never fabricated |
| SDR contracts | `planned-docker`; Linux Docker/OrbStack/Lima Docker; `planned-privileged` external Linux/Lima | S13 SDR1 count/frequency/length/source checks, mTLS commands and correct power results; S14 external baseline excludes simulator, test trust explicit, physical/lab exclusivity; T02 independent radios |
| TAP/VLAN ownership | `planned-privileged`; Linux/Lima separately | S15 TAP UID/GID, peer namespace, VLAN 42 vs 43, bounded mirror and owned cleanup; no guest execution inference |
| Actual guest application | `planned-guest-boot`; x86_64 TCG on Linux x86_64 and Lima ARM64 | S15 checksum-verified echo guest boots, framed config and readiness agree, TCP/UDP both ways, broadcast/multicast/VLAN isolation and QMP pause/resume; T03 actual SDR guest mTLS/UDP/results; missing binary/SAN/handshake timeout fails closed |
| Cleanup/interruption | `planned-portable` and `planned-privileged` where relevant | fail each fixed stage, kill adapter, replace PID/container/ifindex/namespace identity; rollback new owned resources only, retain state on mismatch, no history deletion or existing workload damage |
| Lima access and UI | `planned-privileged` infrastructure setup only when authorized; `planned-docker` platform; explicit browser checks | VM identity checked, chosen console port forwarded loopback-only, occupied host port fails; no privileged socket or UDP forward; graph/external/packet provenance and history displayed accurately |

When executable, use [the repository test procedure](../../docs/test-procedure.md)
and [complete example matrix](../../examples/README.md). Confirm Docker engine
readiness before live container checks; verify OrbStack context on macOS.
Report native Linux, native macOS, OrbStack and Lima independently, with host and
guest architectures. Preserve existing workloads, retain bounded evidence and
compare resource inventories before/after each authorized infrastructure run.
