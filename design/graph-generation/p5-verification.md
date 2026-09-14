# P5 platform verification

P5 implements compiled platform consumption, explicit reference-file credential
staging and rotation, owned bounded history, scoped application credentials,
optional observability services, and the pinned native Node/web companion bundle.
The C++ resolver remains authoritative. Application orchestration, infrastructure,
guest execution and scenario dispatch retain their later phase gates.

## Current behavior

- `graphx-platform` consumes compiled `platform.json`, `resolved.json` and
  `credentials.json`; it does not normalize authored YAML at startup.
- Explicit staging creates private reference directories. Runtime tokens are
  distinct, lab credentials have one ephemeral CA and separate identity/EKU-bound
  leaves, and external credentials must be supplied as private files. Consumers
  receive only their permitted reference directories. Rotation publishes hashed
  generations through stable directories, with bounded previous-token overlap.
- Platform startup, rotation and history deletion reuse the C++ ownership lock
  through process replacement and an inherited descriptor. History records graph
  and owner identity; deletion requires the inactive owner's database. Routine
  shutdown preserves history.
- History accounts for database, WAL and SHM storage together, bounds queues and
  queries, and degrades under storage/query pressure without stopping live
  telemetry. A pinned WAL reader cannot cause unchecked storage growth.
- Existing authentication, scoped grants, replay protection, idempotency and audit
  remain in use. C++ and SDR consumers reload staged credentials; new TLS sessions
  validate complete generations.
- One Dockerfile provides runtime, telemetry and SDR targets. Release builds embed
  the container revisions derived by the release catalog; development builds keep
  source revisions. Images contain production platform modules and web assets.
- Generated Prometheus/Grafana services have fixed digest pins, scoped credentials,
  bounded temporary storage and emitted scrape/alert/dashboard configuration.
  A separate console bridge permits loopback publication and platform egress;
  application management bridges remain internal and UDP is not host-published.
- The sample and simulated-SDR normalization containers and six sample platform
  overlays are removed. Their remaining application/capture wiring and launcher
  execution gates stay in place for P6/P7.

Usage and provisioning commands are in [the platform guide](../../docs/user-guide.md#platform-administration).

## Verification environment and commands

Evidence was collected on 2026-09-13, native macOS ARM64 and the selected OrbStack
ARM64 engine, with Compose v5.1.2. Local image and companion builds explicitly use
`--allow-dirty`; these are verified local candidates, not published release pins.
All container probes use UID/GID 65532, read-only roots, dropped capabilities and
`no-new-privileges`. No privileged tests or operations were run.

```sh
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
PATH=/opt/homebrew/opt/node@24/bin:$PATH GRAPHX_COMPOSE_VALIDATE=1 python3 tests/test_compile.py build/dev/graphx .
PATH=/opt/homebrew/opt/node@24/bin:$PATH node --test apps/telemetry/platform.test.mjs
bash scripts/test-tls.sh build/dev/graphx-tls-smoke
python3 tests/test_sdr_example.py .
python3 tests/test_package.py . build/dev
python3 scripts/release/image_release.py build --output outputs/p5-release-accepted --platform linux/arm64 --allow-dirty
python3 tests/test_platform_docker.py outputs/p5-release-accepted --output outputs/p5-engine-accepted
PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 scripts/release/platform_bundle.py --node-archive outputs/node-v24.20.0-darwin-arm64.tar.gz --output outputs/p5-native-final --allow-dirty
PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 scripts/release/platform_bundle.py --node-archive outputs/node-v24.20.0-darwin-arm64.tar.gz --output outputs/p5-native-final-repeat --allow-dirty
python3 tests/test_platform_bundle.py . build/dev outputs/p5-native-final
shellcheck -x -P . scripts/test-tls.sh scripts/test-history.sh scripts/test-operations.sh scripts/test-secure-otlp.sh scripts/test-compose-features.sh examples/sample-pipeline/scripts/demo.sh
shellcheck -x -P .:examples/sdr-node/simulated/scripts examples/sdr-node/simulated/scripts/demo.sh
shellcheck -s sh outputs/p5-native-launcher.sh
GX_OUTPUT=/tmp/graphx-compiled docker compose -f examples/sample-pipeline/graphx.yml config --quiet
GX_OUTPUT=/tmp/graphx-compiled GRAPHX_SDR_TLS_DIR=/tmp/graphx-tls GRAPHX_SDR_RUN_DIR=/tmp/graphx-run GRAPHX_SDR_RUN_ID=p5-config-check GRAPHX_SDR_PACKET_RULES=/tmp/graphx-rules docker compose -f examples/sdr-node/simulated/graphx.yml config --quiet
git diff --check
```

The final ShellCheck input is extracted from the actual companion archive's
`bin/graphx-platform`. Output directories must be fresh when repeating builds.
The official Node 24.20.0 archive was downloaded with verified TLS and checked
against the reviewed SHA-256 pin; no certificate verification was disabled.

## Results

| Check | Result and evidence under `outputs/` |
|---|---|
| Development CTest | 33 passed; `p5-quick-final-pass.log` |
| C++ formatting, clang-tidy, cppcheck | Passed; `p5-quality-accepted.log` |
| Portable suite | 33 CTests, 101 telemetry tests and 17 web tests passed with no skips, plus the web build; `p5-portable-final.log` |
| Compiler matrix | 63 repeat-byte-identical packages, 15 negative cases, 33 unsupported targets and 51 Compose configurations passed; `p5-compile-accepted.log` |
| Compiled platform integration | Six tests passed: staging/scopes, rotation, lab/external trust, native pipeline/history lifecycle, S01/S11/V01–V06 configuration matrix and authenticated control; `p5-platform-auth-final.log` |
| TLS and SDR | Native staged-generation TLS roundtrip/reconnect and incomplete-generation refusal passed; SDR source-filter and TLS/security checks passed in `p5-sdr-accepted.log` |
| Native C++ archive | Install, external consumer and package validation passed; `p5-package.log` |
| Shared release images | Repeated builds, OCI inspection, catalog derivation and unprivileged executable smoke checks passed; `p5-release-accepted.log`, `p5-release-accepted/images.json` |
| OrbStack acceptance | All nine cases passed, including real Prometheus/Grafana and the packaged SDR reader; `p5-engine-accepted.log`, `p5-engine-accepted/verification.json` |
| Native companion | Two archives reproduced byte-for-byte. Extracted launcher started using bundled Node and served web assets; checksum, release-identity and executable-mode negatives passed; `p5-native-final-execution.log` |
| Shell/Python syntax and whitespace | ShellCheck commands above, Python compilation and `git diff --check` passed |

The native companion archive SHA-256 is
`d05f09a5be388bd1a3c79181ba7fb2f7ef80717040bbb0b9c902602373feef9b`.
Its manifest records the dirty candidate status, Node pin, exact file hashes,
executable modes and release identity. Publication verification requires a clean
candidate with the expected commit/version/platform/epoch.

The native integration runs an actual three-node shared-memory pipeline with
staged per-node HMAC credentials. History tests cover restart persistence,
owner-mismatched and active-writer deletion refusal, aggregate storage pressure,
queue/query limits and a real WAL-pinning reader. Control tests exercise wrong-node
signatures, stale timestamps, replayed nonces, wrong origins, idempotency conflicts,
rotation expiry and durable SQLite audit. Secure OTLP uses the existing integration
suite plus compiled credential mapping and per-request credential reload.

The engine matrix checks S01/S11 zero-application platforms, V01–V06 and the SDR
platform with release-derived catalog artifacts. V02 starts real Prometheus and
Grafana, verifies a healthy authenticated scrape and Grafana database readiness.
The SDR probe invokes the packaged authoritative reader and verifies scoped staged
credentials and rejection of stale type revisions; it does not run an SDR graph.
Only identity-checked test containers, volumes and networks are cleaned up. The
before/after inventory contains the same three pre-existing stopped containers;
all nine test cases completed and left those containers untouched.

## Evidence limits

Native Linux, a separately selected Linux Docker engine, privileged Lima, TCG,
KVM and manual browser interaction were not run. OrbStack containers execute Linux
binaries but are reported only as OrbStack evidence. Web tests/build and HTTP asset
serving do not establish manual browser acceptance. The secure OTLP variant engine
checks establish platform startup, not delivery to a production collector.

No OVS/veth/TAP, namespace, nftables, capture, QEMU or scenario action was realized.
`graphx run` remains `E_PHASE_UNAVAILABLE`. This phase does not establish P6 startup
barriers, application Compose orchestration, guest execution or full release
publication readiness.
