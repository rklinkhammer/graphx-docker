# GraphX Phases 1 to 10 Linux Implementation and Verification Runbook

**Audience:** GraphX implementers and independent verifiers  
**Target:** Ubuntu 24.04 LTS or an equivalent recent native Linux host  
**Repository:** `graphx-docker`  
**Document date:** 2026-09-03  

This runbook explains how to implement, test, and independently verify the
cumulative GraphX work from Phase 1 through Phase 10 without ChatGPT. It uses
the repository scripts as the primary test interface and separates portable
checks from privileged Linux networking and hosted release operations.

Passing the portable suite does not prove native macvlan, IPvlan, Open vSwitch,
network namespace, nftables, or `tc netem` behavior. Passing source inspection
does not count as runtime verification. Record every unavailable check as
unverified.

## 1 Scope of the ten phases

1. Configuration schema, loader, validation, transport factory, and
   infrastructure planning.
2. Runtime lifecycle, bounded queues, cancellation, reconnect, and graceful
   shutdown.
3. Protocol specification, compatibility, framing, and message identities.
4. CI, sanitizers, fuzzing, formatting, static analysis, and stress tests.
5. Authentication, TLS, API validation, secret handling, and container
   hardening.
6. OpenTelemetry export, health and readiness, SLOs, alerts, and dashboards.
7. Durable SQLite telemetry history, retention, restart, and bounded queries.
8. Authorized runtime controls, credential separation and rotation, command
   acknowledgement, idempotency, and audit.
9. Bounded PCAPNG capture, Wireshark dissector, extcap, and capture download.
10. Release identity, compatibility policy, native packages, SBOMs,
    reproducibility, immutable OCI publication, rollback, and support process.

## 2 Source provenance and independent workspaces

Test an exact candidate. Record the commit and every uncommitted or untracked
file. A clean clone of an older commit is not equivalent to an uncommitted
candidate that contains later remediation.

Use two checkouts:

- `/srv/graphx-implementer` for development and the handoff.
- `/srv/graphx-verifier` for independent verification.

Prefer a reviewed commit or a signed source archive. An alternative transfer is
`rsync` over SSH:

```bash
rsync -aH --delete-delay \
  --exclude build --exclude node_modules --exclude web/dist \
  /path/to/graphx-docker/ verifier@linux-host:/srv/graphx-verifier/
```

Use `--delete-delay` only when the destination is a dedicated candidate
directory. The verifier must not reuse the implementer's build directories,
`node_modules`, containers, volumes, or claimed results.

## 3 Host safety

Use a disposable or dedicated Linux host for privileged tests. The native
network laboratories create narrowly named Docker networks, dummy and veth
interfaces, Open vSwitch bridges, network namespaces, nftables rules, and
qdiscs.

Before privileged testing:

1. Confirm the host carries no production traffic.
2. Inspect `docker network ls`, `ip -brief link`, `ip netns list`,
   `ovs-vsctl show`, and `nft list ruleset`.
3. Read each selected example's `README.md`, `up.sh`, and `down.sh`.
4. Run `graphx infra create ... --dry-run` and review the plan.
5. Do not substitute a physical parent interface for an example's isolated
   dummy parent without a separate network review.
6. Stop if a planned GraphX resource name already belongs to another workload.

## 4 Install prerequisites

### 4.1 Native build and analysis tools

```bash
sudo apt-get update
sudo apt-get install -y \
  git cmake ninja-build build-essential \
  clang-18 clang-tools-18 clang-format-18 clang-tidy-18 \
  libclang-rt-18-dev cppcheck \
  libssl-dev openssl curl jq xxd python3 \
  iproute2 nftables openvswitch-switch tcpdump tshark wireshark-common
```

Requirements:

- CMake 3.25 or newer.
- A compiler supporting C++20 and C++23.
- OpenSSL 3 development headers.
- `clang-format-18`, `clang-tidy-18`, and Clang 18 with libFuzzer.
- Python 3 and TShark for release and Wireshark tests.

Verify the installed versions:

```bash
cmake --version
ninja --version
g++ --version
clang++-18 --version
clang-format-18 --version
clang-tidy-18 --version
cppcheck --version
openssl version
python3 --version
tshark --version
```

The repository intentionally names Clang 18 tools explicitly. A newer formatter
is not interchangeable because formatting output changes between major
versions.

### 4.2 Node.js

Install Node.js 24 through your organization's approved distribution method.
Then verify the SQLite module needed by telemetry history:

```bash
node --version
npm --version
node -e "import('node:sqlite').then(() => console.log('node:sqlite available'))"
```

### 4.3 Docker and Open vSwitch

Install a current rootful Docker Engine, Buildx, and Compose v2. Rootless Docker
is not an equivalent environment for the native network laboratories.

```bash
docker version
docker compose version
docker buildx version
docker info
sudo systemctl enable --now openvswitch-switch
sudo ovs-vsctl show
sudo nft list ruleset >/dev/null
```

### 4.4 Private certificate authorities

Do not pipe an unauthenticated installer into a container build. Provide the CA
or a reviewed installer as a BuildKit secret:

```bash
export GRAPHX_CA_CERT=/secure/company-root-ca.crt
# Or, if policy requires a reviewed installer:
export GRAPHX_CERT_INSTALL_SCRIPT=/secure/install-company-ca.sh
scripts/test-linux-container.sh ctest
```

The Linux verifier image contains `bash`, `curl`, and `apt-get`. The telemetry
runtime is Alpine-based and intentionally does not contain `apt-get`; certificates
must be installed in its build stage using Alpine-compatible tooling or a direct
CA secret, not a Debian installer.

## 5 Create the evidence record

Run this in each checkout. Give implementer and verifier runs different IDs.

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
git diff --binary > "$EVIDENCE/candidate-working-tree.patch"
git ls-files --others --exclude-standard > "$EVIDENCE/untracked-files.txt"
cmake --version
g++ --version
clang++-18 --version
openssl version
node --version
npm --version
docker version
docker compose version
sudo ovs-vsctl show
cp prompt/implement.md prompt/verifier.md "$EVIDENCE/"
cp phase_*_handoff.md phase_*_verification.md "$EVIDENCE/" 2>/dev/null || true
```

Do not interpret an empty Git diff as a clean candidate. Inspect both Git status
and the untracked-file list.

## 6 Implementer workflow for each phase

Repeat this cycle for Phases 1 through 10.

1. Read `prompt/implement.md`, the phase ADR, current implementation, tests,
   documentation, and the preceding verifier report.
2. Convert the phase objective into measurable requirements. Record public API,
   wire, configuration, persistence, security, and deployment invariants.
3. Identify failure cases and hard limits before editing. Include malformed
   input, timeouts, cancellation, resource exhaustion, restarts, and cleanup.
4. Make the smallest coherent change. Update production code, tests,
   configuration, and documentation together.
5. Run the phase-focused gate below from a fresh build directory.
6. Run all earlier phase gates and the cumulative gates in section 8.
7. Reconcile documentation claims with observed behavior.
8. Write `phase_N_handoff.md` with requirements, architecture decisions, files,
   exact commands/results, known limitations, verifier risks, and a checklist.
9. Stop. Do not edit the verification report or publish anything.

If a verifier returns `CHANGES REQUIRED`, reproduce each finding, add a
regression test that fails before the fix, implement the remediation, rerun the
cumulative gates, and update the handoff.

## 7 Fresh baseline builds

Use new build directories:

```bash
BUILD23="$PWD/build/linux-$RUN_ID-cxx23"
BUILD20="$PWD/build/linux-$RUN_ID-cxx20"

cmake -S . -B "$BUILD23" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGRAPHX_BUILD_TESTS=ON
cmake --build "$BUILD23" -j "$(nproc)"
ctest --test-dir "$BUILD23" --output-on-failure

cmake -S . -B "$BUILD20" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGRAPHX_BUILD_TESTS=ON
cmake --build "$BUILD20" -j "$(nproc)"
ctest --test-dir "$BUILD20" --output-on-failure
```

The accepted Phase 10 candidate reports 18/18 ordinary CTests in both language
standards. The Release configuration also enforces the exact package contract.
If the count changes, compare `ctest --show-only=json-v1` with the expected test
inventory instead of accepting a lower count.

## 8 Phase focused gates

Each phase is cumulative. A phase passes only when its focused gate and all
earlier gates pass.

### 8.1 Phase 1 configuration and transport factory

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-config-tests|graphx-config-cli|graphx-config-(mixed-network|macvlan|ipvlan-l2|ipvlan-l3|shared-memory)'

"$BUILD23/graphx" validate graphx.yaml
"$BUILD23/graphx" inspect graphx.yaml > "$EVIDENCE/phase1-inspect.txt"

find examples -mindepth 2 -maxdepth 2 -name graphx.yaml -print0 |
while IFS= read -r -d '' cfg; do
  "$BUILD23/graphx" validate "$cfg"
  "$BUILD23/graphx" infra create "$cfg" --dry-run \
    > "$EVIDENCE/$(basename "$(dirname "$cfg")")-create.plan"
done
```

Pass when every checked-in model validates, invalid models fail closed in the
test suite, inspection reports logical and network paths, and dry runs do not
change the host.

### 8.2 Phase 2 lifecycle and bounded transports

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-tests|graphx-transport-lifecycle-stress'

for attempt in $(seq 1 25); do
  ctest --test-dir "$BUILD23" --output-on-failure \
    -R '^graphx-transport-lifecycle-stress$'
done

GRAPHX_BUILD_DIR="$BUILD23" GRAPHX_MAX_MESSAGES=8 GRAPHX_INTERVAL_MS=5 \
  examples/shared-memory/run.sh
```

Pass when timeout, cancellation, end-of-stream, and failure remain distinct;
queues are bounded; reconnect and process-death behavior pass; sequence 8
reaches the sink with value 16; and repeated shutdown releases resources.

### 8.3 Phase 3 protocol and compatibility

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-protocol-boundaries|graphx-mixed-version-transport|graphx-tests'
sha256sum tests/fixtures/envelope-v1.hex tests/fixtures/envelope-v2.hex
```

Expected fixture hashes:

```text
e2bb2e264682d91b87fa4f8cb6e996959dc71b129356e7ace7ba15df1db0a573  tests/fixtures/envelope-v1.hex
c207a51dc5423f3a4f2e47404572f6845ca141937f3dc71d03ff920775aeeda6  tests/fixtures/envelope-v2.hex
```

Pass when golden vectors, truncation, maximum-plus-one, unknown version,
duplicate attribute, trailing-data, identity lineage, and live v1/v2 cases pass.

### 8.4 Phase 4 quality sanitizers and fuzzing

The simplest Linux-equivalent gates use the repository verifier image:

```bash
GRAPHX_FUZZ_SECONDS=30 scripts/test-linux-container.sh quality
scripts/test-linux-container.sh sanitizers
```

Native equivalents are:

```bash
CLANG_FORMAT=clang-format-18 scripts/check-format.sh
CLANG_TIDY=clang-tidy-18 CPPCHECK=cppcheck \
  GRAPHX_QUALITY_BUILD_DIR="$PWD/build/quality-$RUN_ID" \
  scripts/run-static-analysis.sh

CC=clang-18 CXX=clang++-18 cmake --preset sanitizers --fresh
cmake --build --preset sanitizers -j "$(nproc)"
ASAN_OPTIONS='detect_leaks=1:strict_string_checks=1' \
UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1' \
  ctest --preset sanitizers --output-on-failure

CC=clang-18 CXX=clang++-18 GRAPHX_FUZZ_SECONDS=30 \
  GRAPHX_FUZZ_BUILD_DIR="$PWD/build/fuzz-$RUN_ID" scripts/run-fuzz.sh
```

The accepted candidate has 18 enabled sanitizer tests; the release package test
is disabled in a sanitizer build because downstream consumers would need the
same sanitizer runtimes. Pass only with no ASan, UBSan, leak, static-analysis,
format, or fuzzer finding.

### 8.5 Phase 5 TLS authentication and containers

```bash
ctest --test-dir "$BUILD23" --output-on-failure -R '^graphx-tls-security$'
npm ci --prefix apps/telemetry
npm test --prefix apps/telemetry
npm audit --prefix apps/telemetry --omit=dev
npm ci --prefix web
npm test --prefix web
npm run build --prefix web
npm audit --prefix web --omit=dev
docker compose config --quiet
docker compose build
bash scripts/test-container-hardening.sh
```

Pass when TLS 1.3 and mTLS positive/negative cases, HMAC tamper/replay/staleness,
HTTP/WebSocket bounds, origin/method/rate checks, secret redaction, non-root
users, read-only roots, capability drops, PID limits, and capture-volume
initialization order all pass. A registry timeout makes `npm audit` unverified,
not passed.

### 8.6 Phase 6 OTLP health SLO and dashboards

```bash
docker compose --profile observability config --quiet
GRAPHX_PHASE6_TEST_PROJECT="graphx-p6-$RUN_ID" scripts/test-phase6-operations.sh
GRAPHX_PHASE6_OTLP_PROJECT="graphx-p6-otlp-$RUN_ID" \
GRAPHX_PHASE6_OTLP_PORT=18476 scripts/test-phase6-secure-otlp.sh
```

Pass when liveness, service readiness, graph readiness, SLO warm-up and
violations, Prometheus alerts, Grafana provisioning, bounded OTLP queues and
responses, retry classification, absolute deadlines, bearer/private-CA/mTLS,
and shutdown during backoff all behave as documented.

### 8.7 Phase 7 durable telemetry history

```bash
npm test --prefix apps/telemetry
npm test --prefix web
GRAPHX_PHASE7_TEST_PROJECT="graphx-p7-$RUN_ID" \
GRAPHX_PHASE7_OBSERVATION_TOKEN='phase7-observation-token-0123456789abcdef' \
  scripts/test-phase7-history.sh
```

Pass when authenticated operational metadata survives an abrupt container
restart; retention, pagination, query bounds, writer-lock failure, queue
overflow, database size, incompatible schema, graph ownership, and shutdown
deadline tests pass; and credentials or message bodies are never persisted.

### 8.8 Phase 8 authorized control plane

```bash
npm test --prefix apps/telemetry
npm test --prefix web
scripts/test-linux-container.sh portable
```

Inspect the telemetry output for authorization scope, UUID command identity,
idempotent replay, conflicting-key rejection, wrong-target and forged
acknowledgement rejection, timeout, audit authorization, rotation overlap,
cross-role collision, previous-credential projection, and credential-free fan
out. The portable suite must prove real signed source pause and resume: the sent
counter stops only after acknowledgement and advances after resume.

Validate the policy overlay with distinct protected secrets:

```bash
export GRAPHX_CONTROL_SOURCE_TOKEN_FILE=/secure/source.token
export GRAPHX_CONTROL_ADMIN_TOKEN_FILE=/secure/admin.token
export GRAPHX_RUNTIME_GENERATOR_SECRET_FILE=/secure/generator.hmac
export GRAPHX_RUNTIME_TRANSFORM_SECRET_FILE=/secure/transform.hmac
export GRAPHX_RUNTIME_SINK_SECRET_FILE=/secure/sink.hmac
docker compose -f compose.yaml -f compose.control.yaml config --quiet
```

Never place real credentials in `graphx.yaml`, Compose files, logs, or evidence.

### 8.9 Phase 9 PCAPNG Wireshark and extcap

```bash
ctest --test-dir "$BUILD23" --output-on-failure \
  -R 'graphx-(capture-runtime-config|extcap|wireshark-dissector|tests)'
examples/capture/run.sh
tools/graphx-extcap --extcap-interfaces
tools/graphx-extcap --extcap-interface graphx --extcap-dlts
tools/graphx-extcap --extcap-interface graphx-ethernet --extcap-dlts
tools/graphx-extcap --extcap-interface graphx --extcap-config
```

Use `capinfos` and TShark on the generated files. Pass when GraphX application
captures use DLT 147, real mirrored Ethernet uses DLT 1, canonical frames decode,
limits stop capture without stopping graph traffic, unsafe file targets are
rejected before mutation, malformed PCAPNG is rejected, extcap cancellation is
clean, and authenticated downloads remain descriptor-bound and catalog-bounded.

### 8.10 Phase 10 release engineering

Start from a clean tagged candidate whenever possible:

```bash
VERSION=$(tr -d '\n' < VERSION)
python3 scripts/release/validate_version.py --tag "v$VERSION"

REL_A="$PWD/build/release-a-$RUN_ID"
REL_B="$PWD/build/release-b-$RUN_ID"
OUT_A="$PWD/outputs/release-a-$RUN_ID"
OUT_B="$PWD/outputs/release-b-$RUN_ID"

python3 scripts/release/build_release.py \
  --build-dir "$REL_A" --output-dir "$OUT_A" --tag "v$VERSION"
python3 scripts/release/build_release.py \
  --build-dir "$REL_B" --output-dir "$OUT_B" --tag "v$VERSION"
python3 scripts/release/verify_release.py "$OUT_A" --source .
python3 scripts/release/verify_release.py "$OUT_B" --source .

for file in "$OUT_A"/*; do
  cmp "$file" "$OUT_B/$(basename "$file")"
done
sha256sum "$OUT_A"/*
```

For an intentionally uncommitted development candidate only, add
`--allow-dirty` to both build commands and record that fact. Never use it for a
release decision.

Pass when each platform produces exactly four artifacts, both builds are byte-
identical, verification binds trusted source identity, the archive has exactly
69 regular files with the documented `0644`/`0755` modes, SPDX and checksums
match, downstream consumption succeeds, and tests reject every required-file
omission, unexpected file, unsafe member, and mode change.

Validate the release workflow without publishing:

```bash
python3 -m compileall -q scripts/release tests/test_release.py tests/test_package.py
python3 tests/test_release.py . "$BUILD20/graphx"
docker compose --profile observability build
git diff --check
```

Do not create a tag, GitHub release, or GHCR identity during local verification.
OCI multi-architecture staging, digest-bound SBOM/provenance, non-overwrite
promotion, cancellation compensation, and rollback require a controlled
disposable GitHub/GHCR rehearsal.

## 9 Full cumulative gates

After phase-focused checks pass, run the aggregate suites from fresh state:

```bash
scripts/test-linux-container.sh ctest
GRAPHX_FUZZ_SECONDS=30 scripts/test-linux-container.sh quality
scripts/test-linux-container.sh sanitizers
scripts/test-linux-container.sh portable
docker compose config --quiet
docker compose --profile observability config --quiet
docker compose --profile observability build
bash scripts/test-container-hardening.sh
```

Expected accepted-candidate headline results:

- C++23: 18/18 CTests.
- C++20: 18/18 CTests.
- Sanitizers: 18/18 enabled tests; package test disabled by design.
- Telemetry: 70/70 tests.
- Web: 9/9 tests and a successful Vite production build.
- Release archive: 69 regular files with exact portable modes.

Test counts are comparison aids. Always inspect the test inventory and logs.

## 10 Privileged native Linux network gate

Run this only on a dedicated native Linux host:

```bash
sudo -v
GRAPHX_ALLOW_PRIVILEGED_TESTS=1 \
GRAPHX_BUILD_DIR="$PWD/build/linux-net-$RUN_ID-cxx23" \
GRAPHX_CXX20_BUILD_DIR="$PWD/build/linux-net-$RUN_ID-cxx20" \
  scripts/test-features.sh linux-network
```

The gate must exercise real macvlan, IPvlan L2/L3, Open vSwitch, namespace
routing, nftables, and `tc netem`. Docker Desktop or an SSHFS-mounted source tree
is not equivalent. Confirm end-to-end values, expected independent Compose
domains, actual paths, capture frames, fault application/clear, and scoped
cleanup.

For deeper mixed-network evidence:

```bash
examples/mixed-network/scripts/linux-up.sh
examples/mixed-network/scripts/status.sh
examples/mixed-network/scripts/fault.sh apply
sudo tc qdisc show > "$EVIDENCE/tc-applied.txt"
examples/mixed-network/scripts/fault.sh clear
examples/mixed-network/scripts/capture.sh mac captures/mixed-mac.pcapng
examples/mixed-network/scripts/linux-down.sh
```

Capture commands wait for traffic; run them in another terminal and stop them
with `Ctrl-C` after sufficient evidence is collected.

## 11 Independent verifier workflow

The verifier performs these steps in `/srv/graphx-verifier`:

1. Read `prompt/verifier.md`, `prompt/implement.md`, the handoff, phase ADR, and
   prior verification report in full.
2. Record the exact source identity and compare the actual diff with the
   handoff's claimed files and requirements.
3. Inspect boundaries before running tests: ownership, dependency direction,
   bounds, deadlines, input validation, secret handling, cleanup, compatibility,
   and documentation claims.
4. Create fresh C++20/C++23, sanitizer, quality, fuzz, JavaScript, package,
   Compose, and privileged-network state. Do not reuse implementer artifacts.
5. Run the relevant phase-focused gate and every cumulative gate.
6. Add adversarial tests chosen independently from the implementation. Include
   malformed and oversized input, exhaustion, timeout, cancellation, restart,
   credential confusion, unsafe files, archive tampering, and partial failure.
7. Compare README, configuration, schema, ADR, operational docs, examples, and
   handoff claims with observed behavior.
8. Write only `phase_N_verification.md`. Do not quietly fix production code.

Use exactly one verdict:

- `ACCEPTED` when all criteria pass and only non-blocking P3 work remains.
- `CHANGES REQUIRED` when a P1/P2 issue or unmet criterion remains.
- `REJECTED` when the design is unsafe or fundamentally incompatible.
- `BLOCKED` when essential verification cannot run because of an external
  constraint.

The report must contain verdict, executive summary, acceptance matrix, findings,
exact tests/results, unverified areas, compatibility/security assessment,
required remediation, and next-phase readiness. Every finding needs severity,
location, reproduction, expected and actual behavior, impact, remediation, and
a missing regression test.

## 12 Final cleanup and evidence checklist

Preserve source identity, all tool versions, configure/build logs, CTest logs,
analysis/fuzz logs, telemetry/web installs and audits, rendered Compose files
with secrets redacted, image digests, hardening results, operations/history logs,
network before/during/after state, captures, release artifacts, and the explicit
list of unavailable checks.

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

Do not delete a resource of uncertain ownership.

## 13 Troubleshooting

### Dependency fetch fails

The pinned yaml-cpp fallback requires approved network access unless the exact
dependency is already available. Use the organization's package mirror or
approved cache. Do not remove hash verification.

### TLS test returns 141

Exit 141 normally means a process received `SIGPIPE`. Run the verbose focused
test and preserve its output:

```bash
ctest --test-dir "$BUILD23" -R '^graphx-tls-security$' -VV
```

Confirm the candidate contains the transport's no-SIGPIPE handling and rerun in
the Linux verifier container. Do not treat 141 as a certificate failure without
the verbose log.

### npm certificate validation fails

Supply the private root CA as a BuildKit secret. Do not disable TLS validation,
set `strict-ssl=false`, or bake private certificates into source control.

### Fuzzer cannot find support libraries

Use Clang 18 and install `libclang-rt-18-dev`. Verify the compiler selected by
the fuzzer build is `clang++-18`, not GCC or another Clang major.

### A port is already in use

Set unique `GRAPHX_TEST_HTTP_PORT`, `GRAPHX_TEST_UDP_PORT`, Phase 6 OTLP port,
and Compose project names for each run.

### macvlan host cannot reach a child

This is normal parent/child isolation. The acceptance path is container-to-
container traffic unless a separately reviewed host macvlan shim is added.

### npm audit endpoint times out

Keep the application test/build result separate from advisory status. Record
the exact endpoint error and rerun the audit in hosted CI or when registry access
is restored.

## 14 Codex and SSH filesystems

Codex does not provide a dedicated SSHFS mount command. If the environment in
which Codex runs already has an SSH client, FUSE/SSHFS, credentials, network
access, and mount permission, Codex can run the same operating-system mount
command that a user could run. The mount remains an operating-system feature.

Preferred approaches are:

1. Run Codex CLI on the Linux host and work in a local checkout.
2. Transfer an exact candidate with Git or `rsync` over SSH.
3. Use an SSHFS mount for inspection and light editing only.
4. Use the Codex app-server remote connection through a secured or SSH-forwarded
   WebSocket when that experimental workflow is appropriate.

Example user-managed Linux mount:

```bash
sudo apt-get install -y sshfs
mkdir -p /mnt/graphx-remote
sshfs -o reconnect,ServerAliveInterval=15,ServerAliveCountMax=3 \
  verifier@linux-host:/srv/graphx-candidate /mnt/graphx-remote
fusermount3 -u /mnt/graphx-remote
```

Verify the SSH host key, use an agent or short-lived key, mount only a dedicated
directory, and never put credentials in a prompt. Builds, file watchers, locks,
Unix sockets, permissions, and performance can differ over FUSE. Run Docker,
OVS, namespace, nftables, and netem tests on the Linux host against a local
filesystem.

The official Codex CLI reference documents local workspace paths and remote
app-server connections; it does not describe SSHFS as a Codex-managed storage
feature: <https://developers.openai.com/codex/cli/reference/>.
