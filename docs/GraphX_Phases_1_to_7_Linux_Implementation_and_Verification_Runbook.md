# GraphX Phases 1–7

## Linux Implementation and Independent Verification Runbook

**Audience:** GraphX implementers and independent verifiers  
**Target host:** Native Ubuntu 24.04 LTS or an equivalent recent Linux distribution  
**Repository:** `graphx-docker`  
**Document date:** 2026-09-02  
**Purpose:** Reproduce the implementation checks and independently verify the cumulative Phase 1–7 system without ChatGPT.

---

## 1. What this runbook proves

This runbook tests the current cumulative GraphX implementation against the intent of Phases 1 through 7:

1. Configuration schema, loader, validation, and transport factory.
2. Runtime lifecycle, bounded queues, cancellation, reconnect, and graceful shutdown.
3. Protocol specification, compatibility rules, and message/trace identities.
4. CI-equivalent quality gates, sanitizers, fuzzing, static analysis, and expanded transport tests.
5. Authentication, TLS, API validation, and portable-container hardening.
6. OpenTelemetry export, health/readiness, SLOs, Prometheus alerts, and Grafana dashboards.
7. Durable SQLite telemetry history, retention, authenticated queries, and restart persistence.

It includes three levels of testing:

- **Unprivileged cumulative tests:** native builds, CTest, JavaScript tests, static analysis, sanitizers, fuzzing, API tests, and dry-run infrastructure plans.
- **Container tests:** Docker Compose, hardened images, persistent history, Prometheus/Grafana, and secure OTLP/mTLS.
- **Privileged native-Linux laboratories:** real macvlan, IPvlan L2/L3, Open vSwitch, network namespaces, nftables, SPAN capture, and `tc netem`.

Passing only the unprivileged tier does **not** prove Linux network-driver behavior. Passing only code inspection does **not** count as runtime verification.

## 2. Important source-provenance warning

Testing is meaningful only when the Linux host has the exact candidate source.

At the time this runbook was prepared, the observed repository had:

- `HEAD` at `cc57590` (`phase 7 verified`); and
- additional uncommitted Phase 7 remediation files in the working tree.

Therefore, cloning `origin/main` alone may not reproduce the exact remediated candidate that received the latest Phase 7 acceptance. Before transfer, either:

- commit the reviewed candidate to a dedicated internal branch; or
- copy the complete working tree with `rsync`, including untracked source files but excluding build products and dependency directories.

Do not silently mix a clean commit with a separate, unidentified patch.

### Recommended transfer with Git

On the source workstation, create a reviewed candidate branch and commit only after normal project approval. On the Linux host:

```bash
git clone <approved-repository-url> graphx-docker
cd graphx-docker
git switch --detach <exact-approved-commit-sha>
```

### Alternative transfer with rsync over SSH

From the source workstation:

```bash
rsync -aH --delete-delay \
  --exclude '.git/worktrees/' \
  --exclude 'build/' \
  --exclude 'node_modules/' \
  --exclude 'web/dist/' \
  /path/to/graphx-docker/ \
  verifier@linux-host:/srv/graphx-candidate/
```

Review the destination before using `--delete-delay`; it is appropriate only for a dedicated candidate directory.

## 3. Independence model

Use two working copies:

- `/srv/graphx-implementer` — used by the implementer to develop and run handoff checks.
- `/srv/graphx-verifier` — created fresh from the exact same candidate for independent verification.

Best practice is a second person or team. If one person performs both roles, use a fresh checkout, new build directories, new Compose project names, and a separate evidence directory. Do not reuse the implementer's build cache, `node_modules`, containers, volumes, or claimed results.

The verifier may update only the verification report. A failed criterion returns the work to the implementer; the verifier should not quietly repair production code during the verification pass.

## 4. Host safety and isolation

Use a disposable or dedicated Linux test host. The privileged networking tier creates and removes narrowly named Docker networks, dummy interfaces, veth pairs, Open vSwitch bridges, network namespaces, nftables rules, and qdiscs.

Before privileged testing:

1. Confirm the host is not carrying production traffic.
2. Confirm no unrelated resource uses GraphX laboratory names such as `gx-*`, `br-gx-*`, `br-l2-*`, or the example Docker network names.
3. Read each example's `README.md` and `scripts/up.sh`/`down.sh` pair.
4. Run the infrastructure command with `--dry-run` and review every planned action.
5. Keep a second root shell available in case routing or OVS setup needs inspection.
6. Do not substitute a physical parent interface for the examples' isolated dummy parents unless that change has been separately reviewed.

The ordinary CI and portable tiers intentionally do not opt into privileged network mutation.

## 5. Linux prerequisites

### 5.1 Recommended host

- Ubuntu 24.04 LTS, x86-64 or ARM64.
- At least 4 CPU cores, 12 GiB RAM, and 30 GiB free disk.
- Native Linux kernel; do not use Docker Desktop as proof of macvlan/ipvlan behavior.
- Correct system time. HMAC tests allow bounded clock skew.

### 5.2 Base and quality tools

```bash
sudo apt-get update
sudo apt-get install -y \
  git cmake ninja-build build-essential \
  clang-18 clang-tools-18 clang-format-18 clang-tidy-18 libclang-rt-18-dev \
  cppcheck libssl-dev openssl curl jq xxd \
  iproute2 nftables openvswitch-switch tcpdump wireshark-common
```

Verify:

```bash
cmake --version
ninja --version
clang++-18 --version
clang-format-18 --version
clang-tidy-18 --version
cppcheck --version
openssl version
```

Required minimums and pins:

- CMake 3.25 or newer.
- C++20 and C++23 support.
- OpenSSL 3 development headers.
- clang-format **18.x** for the repository format gate.
- Clang 18 with libFuzzer for the Phase 4 fuzz gate.

### 5.3 Node.js and npm

Install Node.js 24.x using the organization's approved Node distribution method. Node 22 or newer is required for the Phase 7 `node:sqlite` implementation; the repository's Linux CI currently uses Node 24.

```bash
node --version
npm --version
node -e "import('node:sqlite').then(() => console.log('node:sqlite available'))"
```

Expected: Node reports `v24.x` and the SQLite probe prints `node:sqlite available`.

### 5.4 Docker

Install a current rootful Docker Engine, Buildx, and the Compose v2 plugin using Docker's approved Linux installation procedure.

```bash
docker version
docker compose version
docker info
```

The invoking user must be able to run the unprivileged container tests. The native macvlan/ipvlan/OVS labs also require `sudo` and a rootful engine. Rootless Docker is not an equivalent result for those labs.

### 5.5 Open vSwitch

```bash
sudo systemctl enable --now openvswitch-switch
sudo ovs-vsctl show
sudo nft list ruleset >/dev/null
ip -Version
```

## 6. Create an evidence record

Run this in the root of the dedicated checkout:

```bash
set -Eeuo pipefail
RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
EVIDENCE="$PWD/evidence/$RUN_ID"
mkdir -p "$EVIDENCE"
umask 077
exec > >(tee -a "$EVIDENCE/session.log") 2>&1

date -u
uname -a
cat /etc/os-release
git rev-parse HEAD
git status --short
git submodule status || true
node --version
npm --version
cmake --version
clang++-18 --version
docker version
docker compose version
sudo ovs-vsctl show
```

Copy the candidate description into the evidence directory:

```bash
git diff --binary > "$EVIDENCE/candidate-working-tree.patch"
git status --porcelain=v1 > "$EVIDENCE/candidate-status.txt"
git ls-files --others --exclude-standard > "$EVIDENCE/untracked-files.txt"
cp prompt/implement.md prompt/verifier.md "$EVIDENCE/"
cp phase_*_handoff.md phase_*_verification.md "$EVIDENCE/" 2>/dev/null || true
```

Do not treat a zero-length patch as proof of cleanliness; also inspect `candidate-status.txt` and `untracked-files.txt`.

## 7. Fresh baseline build

Use new directories rather than deleting or reusing an old build:

```bash
BUILD23="$PWD/build/linux-verify-$RUN_ID-cxx23"
BUILD20="$PWD/build/linux-verify-$RUN_ID-cxx20"

cmake -S . -B "$BUILD23" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON
cmake --build "$BUILD23" -j 4
ctest --test-dir "$BUILD23" --output-on-failure

cmake -S . -B "$BUILD20" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON
cmake --build "$BUILD20" -j 4
ctest --test-dir "$BUILD20" --output-on-failure
```

For the current Phase 7 candidate, each ordinary configuration should report **12/12 CTests passed**. Any failed test is a failed gate. If test counts change in a later approved candidate, record the new count and prove that no expected test disappeared.

## 8. Phase-by-phase implementation gates

Run the following gates in order. Each later phase is cumulative: it must pass its own checks and preserve every preceding phase.

### Phase 1 — Configuration, validation, and transport factory

**Intent:** One authoritative, versioned GraphX model; strict validation; transport-neutral configuration; deterministic transport factory and infrastructure plans.

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-config-tests|graphx-config-cli|graphx-config-(mixed-network|macvlan|ipvlan-l2|ipvlan-l3|shared-memory)'

"$BUILD23/graphx" validate graphx.yaml
"$BUILD23/graphx" inspect graphx.yaml > "$EVIDENCE/phase1-inspect.txt"

while IFS= read -r -d '' cfg; do
  "$BUILD23/graphx" validate "$cfg"
  "$BUILD23/graphx" infra create "$cfg" --dry-run \
    > "$EVIDENCE/phase1-$(basename "$(dirname "$cfg")").plan"
done < <(find examples -mindepth 2 -maxdepth 2 -name graphx.yaml -print0)
```

**Pass criteria:**

- All configuration CTests pass.
- Every checked-in model validates.
- `inspect` shows logical graph, transports, deployment hints, observability, and network path without changing the host.
- Dry-run plans are non-empty where infrastructure is defined and contain no shell interpolation from untrusted configuration.
- Invalid types, missing references, bad addresses/MACs/VLANs, cycles, unknown keys, and transport mismatches are rejected by the test suite.
- Configuration version remains explicitly supported; no silent reinterpretation occurs.

### Phase 2 — Runtime lifecycle and bounded transports

**Intent:** Typed receive outcomes; explicit timeout/cancellation/end-of-stream/failure semantics; bounded queues; reconnect; process-death handling; graceful shutdown across all transports.

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-tests|graphx-transport-lifecycle-stress'

GRAPHX_BUILD_DIR="$BUILD23" \
GRAPHX_MAX_MESSAGES=8 GRAPHX_INTERVAL_MS=5 \
  examples/shared-memory/run.sh
```

**Pass criteria:**

- Transport and lifecycle tests pass without hang, race symptom, or ambiguous result state.
- TCP fragmentation, deadlines, reconnect, `SIGPIPE`, Unix sockets, shared-memory wraparound, ownership, process death, and backpressure cases pass.
- The shared-memory pipeline delivers sequence 8 with value 16 and cleans up its IPC resources.
- SIGINT/SIGTERM paths release sockets, mappings, queues, threads, and files within bounded time.

Repeat the lifecycle stress test to expose intermittent failures:

```bash
for attempt in $(seq 1 25); do
  ctest --test-dir "$BUILD23" --output-on-failure \
    -R '^graphx-transport-lifecycle-stress$'
done
```

### Phase 3 — Protocol and compatibility

**Intent:** Normative v1/v2 envelope and framing rules; bounded parser inputs; exact golden fixtures; stable message/trace/parent identity; mixed-version behavior.

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-protocol-boundaries|graphx-mixed-version-transport|graphx-tests'

sha256sum tests/fixtures/envelope-v1.hex tests/fixtures/envelope-v2.hex
```

Expected fixture hashes for this candidate:

```text
e2bb2e264682d91b87fa4f8cb6e996959dc71b129356e7ace7ba15df1db0a573  tests/fixtures/envelope-v1.hex
c207a51dc5423f3a4f2e47404572f6845ca141937f3dc71d03ff920775aeeda6  tests/fixtures/envelope-v2.hex
```

**Pass criteria:**

- Exact v1 and v2 golden vectors pass.
- Maximum and maximum-plus-one boundaries behave deterministically.
- Every proper truncated v2 prefix is rejected.
- Unknown versions, malformed lengths, trailing data, oversized attributes, and invalid identities fail closed.
- One live TCP connection accepts v1 then v2 without identity drift.
- Derivation creates a new message ID while preserving trace identity and recording the parent.

### Phase 4 — Quality automation, sanitizers, fuzzing, and analysis

**Intent:** Reproducible, pinned quality gates that cover all GraphX-owned targets without placing sanitizer/fuzzer runtimes into production images.

#### Formatting and static analysis

```bash
CLANG_FORMAT=clang-format-18 scripts/check-format.sh

CLANG_TIDY=clang-tidy-18 CPPCHECK=cppcheck \
GRAPHX_QUALITY_BUILD_DIR="$PWD/build/linux-verify-$RUN_ID-quality" \
  scripts/run-static-analysis.sh
```

#### ASan, UBSan, and Linux leak detection

```bash
export CC=clang-18
export CXX=clang++-18
cmake --preset sanitizers --fresh
cmake --build --preset sanitizers -j 4
ASAN_OPTIONS='detect_leaks=1:strict_string_checks=1' \
UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1' \
  ctest --preset sanitizers
unset CC CXX
```

Expected current count: **13/13**, including `graphx-sanitizer-coverage`.

#### Bounded libFuzzer smoke

```bash
CC=clang-18 CXX=clang++-18 \
GRAPHX_FUZZ_BUILD_DIR="$PWD/build/linux-verify-$RUN_ID-fuzz" \
GRAPHX_FUZZ_SECONDS=30 \
  scripts/run-fuzz.sh
```

**Pass criteria:**

- Format, clang-tidy, and Cppcheck gates exit 0.
- All sanitizer CTests pass with no ASan, UBSan, or leak finding.
- The sanitizer-coverage audit proves every GraphX-owned target was compiled with instrumentation.
- Both envelope and frame fuzzers run for at least 30 seconds and exit cleanly with no crash, timeout, leak, or sanitizer finding.
- An unavailable tool is recorded as **unverified**, never as passed.

### Phase 5 — Authentication, TLS, API validation, and hardening

**Intent:** TLS 1.3/mTLS, strict and bounded untrusted-input handling, HMAC anti-replay telemetry/control, distinct observation/control credentials, and least-privilege portable containers.

```bash
ctest --test-dir "$BUILD23" --output-on-failure -R '^graphx-tls-security$'

npm ci --prefix apps/telemetry
npm test --prefix apps/telemetry
npm audit --prefix apps/telemetry --omit=dev

npm ci --prefix web
npm test --prefix web
npm run build --prefix web
npm audit --prefix web --omit=dev

docker compose -f compose.yaml config --quiet
docker compose -f compose.yaml build
bash scripts/test-container-hardening.sh
```

The current cumulative candidate expects **36 telemetry tests** and **six web tests**. These totals include later-phase checks, which is desirable because the security boundary is cumulative.

Also run the portable acceptance suite in a fresh pair of build directories:

```bash
GRAPHX_BUILD_DIR="$PWD/build/linux-verify-$RUN_ID-portable-cxx23" \
GRAPHX_CXX20_BUILD_DIR="$PWD/build/linux-verify-$RUN_ID-portable-cxx20" \
GRAPHX_TEST_HTTP_PORT=18175 \
GRAPHX_TEST_UDP_PORT=19175 \
  scripts/test-features.sh portable
```

**Pass criteria:**

- TLS positive and negative cases pass: peer identity, trust, missing certificate, reconnect, deadlines, and cancellation.
- Malformed HTTP/WebSocket targets do not terminate the service; a later health request succeeds.
- HMAC rejects tampering, stale timestamps, replay, unknown identities, and oversized data before state mutation.
- Observation and control credentials remain distinct; control requires both bearer and authenticated runtime channel.
- HTTP methods, origins, headers, bodies, target lengths, rates, WebSocket frames, and state maps remain bounded.
- Web tests prove session-scoped observation state, in-memory-only control state, authenticated WebSocket/fetch/control behavior, and cleanup on token changes.
- Containers run non-root, use read-only roots, drop all capabilities, set no-new-privileges, bound PIDs, and use a temporary `/tmp`.
- Capture volume initialization works in both image orders; native nodes can write and telemetry can only read.

**Repository documentation warning:** the checked-in `phase_5_verification.md` observed while preparing this guide still says `CHANGES REQUIRED`, even though later code and tests address its findings. A current Linux verifier must rerun Phase 5 and issue a fresh verdict. Do not claim an uninterrupted accepted chain based only on the existing report file.

### Phase 6 — OTLP, health, SLOs, Prometheus, and Grafana

**Intent:** Bounded secure OTLP/HTTP export; distinct liveness/service/graph readiness; bounded rolling SLOs; operational alerts and dashboards; no graph blocking on telemetry failure.

#### Static Compose and Prometheus assets

```bash
docker compose -f compose.yaml -f compose.observability.yaml config --quiet

docker run --rm --entrypoint /bin/promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  check config /etc/prometheus/prometheus.yml

docker run --rm --entrypoint /bin/promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  -w /etc/prometheus \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  test rules alerts.test.yml
```

#### Live operations stack

```bash
GRAPHX_PHASE6_TEST_PROJECT="graphx-p6-$RUN_ID" \
  scripts/test-phase6-operations.sh
```

#### Secure bearer/private-CA/mTLS export

```bash
GRAPHX_PHASE6_OTLP_PROJECT="graphx-p6-otlp-$RUN_ID" \
GRAPHX_PHASE6_OTLP_PORT=18476 \
  scripts/test-phase6-secure-otlp.sh
```

**Pass criteria:**

- OTLP paths, JSON types, non-zero identifiers, decimal nanoseconds, resource attributes, retry classification, absolute deadlines, byte/item queues, response cap, and shutdown cancellation pass.
- Plaintext remote OTLP is rejected unless explicitly acknowledged.
- Bearer token, private CA, and optional client certificate/key are projected outside `graphx.yaml`; secrets do not appear in logs or metrics.
- `/api/live`, `/api/ready`, `/api/graph/ready`, `/api/health`, and `/api/slo` retain distinct meanings.
- Prometheus becomes healthy, reports the GraphX target `up`, returns `graphx_service_ready = 1`, and loads all expected alert rules.
- Grafana provisions dashboard UID `graphx-operations`.
- SLO warm-up cannot trigger a violation alert.
- The scripts remove their isolated Compose projects and ephemeral credentials.

### Phase 7 — Durable telemetry history

**Intent:** Optional metadata-only SQLite persistence; isolated worker ownership; bounded queues/queries/storage/shutdown; strict read-only authenticated API; retention at startup, idle, query, write, and close; persistence across restart.

#### Configuration and container rendering

```bash
export GRAPHX_OBSERVATION_TOKEN='phase7-linux-observation-token-0123456789abcdef'
docker compose -f compose.yaml -f compose.history.yaml config --quiet
unset GRAPHX_OBSERVATION_TOKEN
```

#### Unit, failure-path, and console regressions

```bash
npm test --prefix apps/telemetry
npm test --prefix web
```

Confirm the telemetry output includes passing cases for:

- persistence and stable pagination;
- reduced age/count retention before reopened-store readiness;
- query-time non-disclosure and idle physical pruning;
- maintenance failure under a SQLite writer lock;
- queue item/byte overflow and database-full behavior;
- incompatible schema and graph ownership;
- bounded shutdown while another SQLite writer holds a lock;
- full-service startup rejection for invalid history configuration; and
- authenticated history API across abrupt restart.

Confirm the web output includes the mounted history component test for paused polling, cursor paging, return-to-newest, reordered responses, credential change, and unmount.

#### Live persistent restart

```bash
GRAPHX_PHASE7_TEST_PROJECT="graphx-p7-$RUN_ID" \
GRAPHX_PHASE7_OBSERVATION_TOKEN='phase7-linux-observation-token-0123456789abcdef' \
  scripts/test-phase7-history.sh
```

**Pass criteria:**

- History is disabled by default and does not affect service readiness.
- Enabling the overlay creates a narrowly scoped writable history volume while the root filesystem remains read-only.
- An authenticated UDP event is written, queried, retained through a real telemetry-container restart, and read again.
- No-auth history access returns 401; invalid limits/keys/topology identifiers return 400; POST returns 405.
- An existing store reopened with 60-second and 10-record limits reports ready with no expired row and no more than 10 rows.
- Expired rows are hidden immediately and physically pruned during bounded idle maintenance.
- A maintenance or write failure degrades history without blocking telemetry or graph progress.
- Persisted rows contain operational metadata only—not message bodies, bearer/HMAC/OTLP credentials, control tokens, or capture contents.

## 9. Full cumulative portable and Docker gates

After all phase-focused checks pass, run the aggregate suites from fresh directories.

### Portable aggregate

```bash
GRAPHX_BUILD_DIR="$PWD/build/linux-final-$RUN_ID-cxx23" \
GRAPHX_CXX20_BUILD_DIR="$PWD/build/linux-final-$RUN_ID-cxx20" \
GRAPHX_TEST_HTTP_PORT=18177 \
GRAPHX_TEST_UDP_PORT=19177 \
  scripts/test-features.sh portable
```

Expected coverage includes both language standards, all CTests, every checked-in topology, infrastructure dry runs, finite TCP/shared-memory pipelines, PCAPNG/extcap checks, coordinated shutdown, telemetry and web tests, authenticated real pause/resume, HTTPS, strict API behavior, and secure bind defaults.

### Docker aggregate

Ensure port 8080 is unused, then run:

```bash
scripts/test-features.sh docker
```

Expected: the portable tier passes again, the standard bridge Compose deployment starts, five demo verification lines report `PASS`, service state is healthy, and the suite performs scoped cleanup.

## 10. Privileged native-Linux network verification

This tier is mandatory if the acceptance claim includes real macvlan, IPvlan, OVS, namespace routing, nftables, or netem behavior.

### 10.1 Preflight

```bash
test "$(uname -s)" = Linux
docker ps
docker network ls
ip -brief link
sudo ip netns list
sudo ovs-vsctl show
sudo nft list ruleset > "$EVIDENCE/nft-before.txt"
```

Review plans:

```bash
for cfg in \
  examples/macvlan/graphx.yaml \
  examples/ipvlan-l2/graphx.yaml \
  examples/ipvlan-l3/graphx.yaml \
  examples/mixed-network/graphx.yaml; do
  "$BUILD23/graphx" validate "$cfg"
  "$BUILD23/graphx" infra create "$cfg" --dry-run
done
```

### 10.2 Automated native network tier

```bash
sudo -v
GRAPHX_ALLOW_PRIVILEGED_TESTS=1 \
GRAPHX_BUILD_DIR="$PWD/build/linux-net-$RUN_ID-cxx23" \
GRAPHX_CXX20_BUILD_DIR="$PWD/build/linux-net-$RUN_ID-cxx20" \
  scripts/test-features.sh linux-network
```

This runs portable acceptance, then the macvlan, IPvlan L2, IPvlan L3, and mixed-network labs. The mixed lab also applies and clears its `tc netem` fault.

### 10.3 Manual inspection while a mixed lab is active

For deeper evidence, start the mixed lab separately:

```bash
examples/mixed-network/scripts/linux-up.sh
examples/mixed-network/scripts/status.sh
docker logs --tail 100 gx-ipv-side-sink-1

examples/mixed-network/scripts/fault.sh apply
sudo tc qdisc show > "$EVIDENCE/tc-with-fault.txt"
examples/mixed-network/scripts/fault.sh clear

examples/mixed-network/scripts/capture.sh mac captures/mixed-mac.pcapng
examples/mixed-network/scripts/capture.sh ipv captures/mixed-ipv.pcapng
```

Capture commands wait for traffic; run them in separate terminals or with a bounded timeout appropriate to the host. Inspect PCAPNG files with `capinfos`, `tshark`, or Wireshark.

Clean up in the documented order—containers before external infrastructure:

```bash
examples/mixed-network/scripts/linux-down.sh
```

### 10.4 Network pass criteria

- Each `up.sh` reaches a real end-to-end sink value before its deadline.
- Every node is deployed in the expected independent Docker project/domain.
- macvlan uses explicit IP/MAC assignments on the isolated parent.
- IPvlan L2 traffic crosses the OVS/namespace router and nftables policy.
- IPvlan L3 uses the supported one-network/multiple-subnet layout and no fake gateway claim.
- Mixed traffic crosses macvlan → OVS → namespace router → OVS → IPvlan.
- OVS SPAN capture contains standard Ethernet frames.
- `tc netem` appears on the selected interface when applied and is absent after clear.
- `down.sh` removes only the laboratory's containers, networks, namespaces, OVS bridges, veth/dummy interfaces, policies, and qdiscs.

## 11. Independent verifier procedure

The verifier should perform the following sequence in `/srv/graphx-verifier`.

### Step 1 — Establish scope

1. Read `prompt/implement.md` and `prompt/verifier.md` in full.
2. Read the candidate handoff and relevant ADRs.
3. Record the exact commit, working-tree patch, untracked files, tool versions, host, and date.
4. Compare the source inventory with the implementer's claimed file list.
5. Identify pre-existing limitations separately from candidate regressions.

### Step 2 — Inspect before executing

Review ownership and dependency direction, bounds, deadlines, error states, shutdown, secret handling, input validation, configuration versioning, container privileges, and documentation claims. Do not count appearance as proof; use inspection to choose adversarial tests.

### Step 3 — Run fresh tests

Run sections 7–10 using verifier-specific build directories, ports, and Compose project names. Do not copy the implementer's `build/`, `node_modules/`, images, or volumes.

### Step 4 — Add targeted adversarial checks

At minimum, independently confirm:

- malformed/truncated/oversized protocol data;
- timeout, cancellation, disconnect, restart, repeated startup/shutdown, and queue exhaustion;
- malformed HTTP and WebSocket targets followed by a successful health request;
- missing/wrong observation and control credentials;
- HMAC tamper, replay, staleness, and unknown identity;
- TLS name/CA/client-certificate failures and reconnect;
- OTLP refusal, slow response, retryable/permanent status, bounded queue, and shutdown during backoff;
- history reduced-policy reopen, idle expiry, writer lock, schema mismatch, other graph, and abrupt restart;
- read-only roots, users, groups, capabilities, PID limits, volume initialization order, and writable paths; and
- real Linux macvlan/ipvlan/OVS/netns/nftables/netem behavior when in scope.

### Step 5 — Reconcile documentation with runtime

Compare `README.md`, `docs/test-procedure.md`, the phase-specific docs/ADRs, `graphx.yaml`, schema, Compose projections, and handoff claims with observed behavior. A stale acceptance report is a documentation gap even when code now passes.

### Step 6 — Write the verdict

Use exactly one verdict:

- **ACCEPTED:** every acceptance criterion passes; only non-blocking P3 improvements may remain.
- **CHANGES REQUIRED:** one or more P1/P2 findings or unmet criteria remain.
- **REJECTED:** the design is unsafe, fundamentally incompatible, or does not implement the phase.
- **BLOCKED:** essential verification cannot run because of an external constraint.

For each finding include severity, title, exact component/location, reproduction, expected and actual behavior, impact, remediation, and missing regression test.

The report must contain:

1. Verdict.
2. Executive summary.
3. Acceptance matrix.
4. Findings ordered by severity.
5. Exact commands and results.
6. Unverified areas and why.
7. Compatibility and security assessment.
8. Required remediation.
9. Readiness for the next phase and invariants to preserve.

Do not recommend Phase 8 until Phases 1–7 have a current accepted chain. In this repository snapshot, Phase 1 has no checked-in handoff/verifier report, Phase 2 has a handoff but no separate verification file, and the checked-in Phase 5 verification is still `CHANGES REQUIRED`. Those documentary gaps require either recovered historical evidence or fresh retrospective reports.

## 12. Final evidence checklist

Before signing the report, preserve:

- exact source SHA and candidate patch/untracked manifest;
- OS, kernel, compiler, CMake, OpenSSL, Node/npm, Docker/Compose, OVS, nftables, and analysis-tool versions;
- C++20 and C++23 configure/build logs;
- complete ordinary and sanitizer CTest logs;
- formatting, clang-tidy, Cppcheck, and fuzzer logs;
- telemetry/web install, test, build, and audit logs;
- Compose rendered configurations with secrets redacted;
- image IDs/digests and container-hardening inspection;
- live Phase 6 and Phase 7 script output;
- native-network before/during/after state, qdisc state, OVS state, namespace state, and capture metadata;
- explicit list of skipped or unavailable checks; and
- final cleanup inventory.

Final cleanup check:

```bash
docker ps -a
docker network ls
docker volume ls
sudo ip netns list
sudo ovs-vsctl show
ip -brief link
sudo nft list ruleset > "$EVIDENCE/nft-after.txt"
git diff --check
git status --short
```

Do not delete an unexpected resource. Investigate ownership first.

## 13. Troubleshooting

### First CMake configure cannot fetch yaml-cpp

The fallback dependency is pinned and hash-verified but requires network access if yaml-cpp 0.9.0 is not already available. Allow the approved source temporarily or provide the exact dependency through the organization's package/cache process.

### Port already in use

Change `GRAPHX_TEST_HTTP_PORT`, `GRAPHX_TEST_UDP_PORT`, and the Phase 6 OTLP port. Keep each verifier run unique.

### `node:sqlite` is missing

The Node runtime is too old or was built without the required module. Use Node 24.x as in CI.

### Formatter reports the wrong major

Run `CLANG_FORMAT=clang-format-18 scripts/check-format.sh`. A newer formatter is not an equivalent result because formatting output can change across majors.

### Fuzzer fails before executing

Confirm `CC=clang-18`, `CXX=clang++-18`, and that the Linux Clang package includes libFuzzer. Record a pre-execution failure as unverified.

### Native network resource already exists

Stop. Inspect it before cleanup. If it belongs to a prior GraphX lab and no lab containers remain, use that example's matching `down.sh`, then rerun. The scripts intentionally refuse to overwrite existing resources.

### macvlan host cannot reach a child

That is normal macvlan parent/child isolation. End-to-end container traffic—not host-to-child ping—is the acceptance path unless a separately reviewed host macvlan shim is added.

### Prometheus is healthy but the target is not yet up

Process readiness can precede the first scrape. Use `scripts/test-phase6-operations.sh`; it polls all conditions under one deadline and emits diagnostics on timeout.

## 14. Can Codex mount a remote filesystem through SSH?

There is no documented first-class Codex command that mounts an SSH filesystem. Codex operates through the controlled shell and filesystem permissions of the environment where it is running. If that local environment already has SSH access, an SSH client, an SSHFS/FUSE implementation, and permission to create a mount, Codex can potentially execute the same operating-system commands a user would execute. The mount is an OS feature, not a Codex storage feature.

Practical options, in preferred order:

1. **Run Codex on the Linux host** and open the repository locally there. This gives builds, Docker, OVS, network namespaces, and filesystem semantics their native environment.
2. **Use Git or rsync over SSH** to place an exact candidate in a local Linux directory. This is the most reproducible verification workflow.
3. **Mount with SSHFS outside Codex**, then open the mounted directory as the workspace. This can work for source inspection and light edits, but build tools, file watchers, locks, Unix sockets, permissions, and performance can behave differently across FUSE.
4. **Use ordinary SSH commands from Codex** for bounded remote actions when policy allows. This does not make the remote tree a local workspace and requires careful quoting, authentication, host-key verification, and output/evidence handling.

Example user-managed mount on Linux:

```bash
sudo apt-get install -y sshfs
mkdir -p /mnt/graphx-remote
sshfs -o reconnect,ServerAliveInterval=15,ServerAliveCountMax=3 \
  verifier@linux-host:/srv/graphx-candidate /mnt/graphx-remote
```

Unmount with:

```bash
fusermount3 -u /mnt/graphx-remote
```

Security requirements:

- verify the SSH host key before first use;
- prefer an agent or short-lived key rather than embedding a password/key in prompts;
- grant the remote account only the access needed;
- never expose production secrets to the workspace;
- mount a dedicated directory, not the remote root filesystem; and
- do not run the native privileged network suite through SSHFS. Run it on the Linux host against a local checkout.

Official OpenAI documentation describes the model's shell interaction as a controlled local-computer interface; it does not establish native SSHFS mounting as a Codex product capability: <https://developers.openai.com/api/docs/guides/latest-model?model=gpt-5.2#using-tools-with-gpt-52>.

## Appendix A — Recommended execution order

For one complete current-candidate verification:

1. Transfer and fingerprint the exact candidate.
2. Install and record prerequisites.
3. Create the evidence directory.
4. Run fresh C++23 and C++20 builds and all CTests.
5. Run Phase 1–3 focused checks.
6. Run format, static analysis, ASan/UBSan/LSan, and fuzzing.
7. Run telemetry and web tests/build/audits.
8. Run the fresh portable aggregate.
9. Validate and build Compose images; run hardening checks.
10. Run Phase 6 operations and secure OTLP tests.
11. Run Phase 7 persistent-history test.
12. Run the Docker aggregate.
13. On a dedicated host, run the privileged native-network tier.
14. Inspect cleanup and preserve evidence.
15. Have the independent verifier repeat the required gates from a fresh checkout and issue the verdict.

## Appendix B — Current expected headline results

For the remediated candidate inspected on 2026-09-02:

- Ordinary C++23: 12/12 CTests.
- Ordinary C++20: 12/12 CTests.
- Sanitizers: 13/13 CTests, including coverage audit.
- Telemetry: 36/36 tests.
- Web: 6/6 tests plus a successful Vite production build.
- Portable suite: passed.
- Compose rendering and telemetry image build: passed.
- Phase 6 operations and secure OTLP/mTLS scripts: passed.
- Phase 7 authenticated write/restart/reread script: passed.
- Phase 7 independent verdict: accepted.

These counts are comparison aids, not substitutes for fresh Linux evidence.
