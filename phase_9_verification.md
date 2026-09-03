# Phase 9 independent verification report

Date: 2026-09-03

Repository: `/Users/rklinkhammer/workspace/graphx-docker`

Baseline commit: `8dbb08a`

Work package: PCAPNG, Wireshark dissector, and extcap implementation

## 1. Verdict

**ACCEPTED**

All Phase 9 acceptance criteria are met. The previously reported schema/runtime
divergence, unbounded telemetry catalog, missing configuration-test deadline,
and 512-byte PCAPNG-header assumption are remediated and independently
exercised. No P0, P1, or P2 finding remains. Two non-blocking P3 maintenance
items are recorded below.

## 2. Executive summary

GraphX now provides a transport-neutral, bounded PCAPNG capture sink for exact
canonical framed envelopes, a separate Ethernet PCAPNG representation, a Lua
Wireshark dissector for wire versions 1 and 2, and a validating extcap adapter.
The implementation does not change the GraphX wire format or configuration
version and does not make graph semantics depend on Docker or Wireshark.

The capture writer applies finite packet, file, snap-length, and metadata
bounds; validates the output descriptor before truncation; rejects symlinks,
hard links, FIFOs, sockets, devices, and other non-regular targets; and disables
only capture on limit or I/O failure. Extcap validates complete bounded PCAPNG
blocks, one supported section/interface profile, DLT, and packet interface
references before forwarding. The dissector rejects malformed frames,
unsupported versions, mandatory zero identities, duplicate attribute keys,
oversized counts, truncation, and trailing bytes.

Telemetry preserves observation authorization, validates and streams the same
descriptor, bounds both directory entries inspected and files returned, sorts
the bounded result, caches it for one second, and reports scanned/truncated
metadata. Direct authorized retrieval remains independent of catalog inclusion.
An isolated live stack confirmed that a control credential cannot cross the
observation boundary.

Clean host, sanitizer, Linux portable, Linux static-analysis/fuzz, telemetry,
web, Compose, image, volume-hardening, and isolated runtime checks passed. Both
explicit npm advisory calls timed out at the registry endpoint; lockfile
installation and dependency-tree validation passed, so this is recorded as an
external unverified check rather than an acceptance blocker.

## 3. Acceptance-criteria matrix

| Requirement | Status | Independent evidence |
|---|---|---|
| Write exact canonical application frames as valid PCAPNG | PASS | Native writer tests and independently generated fixtures passed; TShark decoded DLT 147 captures. |
| Preserve real Ethernet capture as a distinct representation | PASS | `EthernetPcapngCaptureSink`, DLT 1 tests, extcap Ethernet interface, and documentation agree. |
| Bound snap length, file bytes, packet count, and metadata | PASS | Native negative/boundary tests passed; Compose runtime reported 4096/1048576/5 and each file stopped at exactly five packets. |
| Keep capture failure isolated from graph processing | PASS | Native tests and live Compose services remained running after capture reached its packet limit. |
| Reject unsafe writer targets before blocking, mutation, or data loss | PASS | CTest covers symlink, hard link, FIFO, socket, device, existing-file preservation, partial-write handling, and bounded completion. |
| Retain correlation without changing canonical bytes | PASS | Writer fixtures preserve the exact framed envelope and bounded PCAPNG comments containing message/trace metadata. |
| Decode GraphX wire versions 1 and 2 in real Wireshark/TShark | PASS | Host and Ubuntu TShark tests passed with expected fields and expert diagnostics. |
| Reject malformed or unsupported dissector input | PASS | Real-TShark mutation tests cover length, magic, version, identities, duplicate keys, counts, truncation, and trailing bytes. |
| Provide functional, bounded extcap discovery/configuration/capture | PASS | Python adversarial suite passed copy/follow, DLT, filter, block, trailer, ordering, interface, symlink, FIFO, and cancellation cases. |
| Validate complete initial PCAPNG blocks without a 512-byte assumption | PASS | Direct verifier probe accepted a 256 KiB SHB and rejected a 256 KiB + 4 byte SHB; telemetry regression also covers a header larger than 512 bytes. |
| Protect capture list/download with observation authorization | PASS | Isolated runtime returned 401 without a token and with the control token; the observation token succeeded. |
| Validate and stream one safe descriptor | PASS | Rename/replacement regression, hard-link/FIFO rejection, size/header/DLT checks, and direct download passed. |
| Bound telemetry catalog work and payload | PASS | Direct 5,002-entry probe inspected 32 entries and returned 8 in 0.651 ms with truncation; live Compose returned 2 of 3 files with explicit metadata. |
| Keep direct downloads independent of truncated listing | PASS | The omitted `transform.pcapng` downloaded and decoded as five GraphX packets. |
| Align schema, native, telemetry, YAML, environment, and Compose semantics | PASS | AJV full-document test, native loader tests, runtime environment tests, Compose validation, and negative scalar/provider/path cases passed. |
| Terminate a stalled configuration test with useful diagnostics | PASS | CTest reports a 30-second timeout; output is unbuffered; 50 consecutive repetitions passed in 5.98 seconds. |
| Use least-privilege container deployment and compatible volume ownership | PASS | Compose hardening inspection and both volume-initialization orders passed; telemetry capture mount is read-only. |
| Document representation, limits, security, Wireshark/extcap operation, and tradeoffs | PASS | README, capture/observability/security/protocol/test docs, example guide, Wireshark guide, and ADR 0010 match observed behavior. |
| Preserve compatibility and Phase 9 scope | PASS | Configuration remains v1, envelope v1/v2 and framing are unchanged, APIs are additive, and no Phase 10 packaging/support work was introduced. |

## 4. Findings ordered by severity

### P3 — The one-second catalog cache has no direct regression assertion

- **Component/location:** `apps/telemetry/server.mjs` catalog cache and
  `apps/telemetry/capture.test.mjs` catalog tests.
- **Evidence/reproduction:** The tests assert bounded and sorted results through
  `/api/captures` and `/api/topology`, but they do not prove that two requests
  inside the one-second interval perform only one directory refresh or that a
  refresh occurs after expiry. Code inspection confirms the cache is present.
- **Expected behavior:** A regression test should fail if snapshot requests once
  again scan the capture directory on every request.
- **Actual behavior:** Runtime behavior is correct, but removal or accidental
  invalidation of the cache would not be detected directly.
- **Impact:** A future regression could multiply synchronous bounded filesystem
  work under request load. Current resource bounds prevent an unbounded scan,
  so this does not block Phase 9.
- **Remediation:** Inject the catalog loader/clock or use a controlled directory
  mutation test to assert one refresh within the TTL and a second after expiry.
- **Missing regression test:** Explicit cache-hit and cache-expiry assertions.

### P3 — Linux verifier context includes ignored local `build-*` directories

- **Component/location:** `.dockerignore` and
  `docker/linux-verifier.Dockerfile` (`COPY . .`).
- **Evidence/reproduction:** The quality build transferred about 89.36 MB while
  `build-phase2` occupied about 84 MB. `.dockerignore` excludes `build` but not
  sibling names such as `build-phase2`. This condition predates Phase 9.
- **Expected behavior:** Local generated build trees should not enter the build
  daemon context.
- **Actual behavior:** They are not copied into production images, but they are
  sent for the broad Linux-verifier context.
- **Impact:** Extra build latency and unnecessary exposure of local generated
  artifacts to the local build daemon; no runtime or image-content defect.
- **Remediation:** Add anchored ignore rules for the repository's generated
  `build-*` directories, or change the verifier image to copy only required
  source paths.
- **Missing regression test:** A lightweight repository/context hygiene check.

## 5. Tests and checks run with exact results

| Check | Command or scope | Exact result |
|---|---|---|
| Repository state and scope | Status, baseline, full prompts/handoff, diffs, implementation, tests, docs, configuration, lockfiles | Baseline `8dbb08a`; Phase 9 remains uncommitted. Only this verification report was verifier-authored. |
| Clean host build and complete CTest | Fresh `/tmp/graphx-phase9-verify-native.E5YWY5`, Debug, C++23, AppleClang 21 | Configure/build passed; 15/15 tests passed in 6.78 s, including real local TShark. |
| Focused verbose Phase 9 tests | CTest regex for config, runtime capture config, extcap, and dissector | 4/4 passed in 1.60 s; CTest displayed the 30-second config-test deadline. |
| Configuration repetition | `graphx-config-tests` repeated until failure 50 times | 50/50 passed in 5.98 s. |
| Host sanitizers | Fresh sanitizer preset configure/build and complete CTest | 16/16 passed in 11.58 s, including sanitizer-coverage audit. |
| Telemetry tests | `npm test` in `apps/telemetry` | 70/70 passed in 5.586 s. |
| Web tests and production build | `npm test` and `npm run build` in `web` | 9/9 passed in 0.650 s; Vite build passed in 3.73 s. Non-fatal main-chunk warning remains at about 1.85 MB. |
| Linux quality/static/fuzz | `GRAPHX_FUZZ_SECONDS=5 scripts/test-linux-container.sh quality` | clang-format-18 passed 39 files; clang-tidy-18, cppcheck, sanitizer coverage, and both five-second fuzz targets passed. Evidence: `outputs/linux-container/quality-20260903T232145Z.log`. |
| Linux portable suite | `scripts/test-linux-container.sh portable` | Ubuntu 24.04/GCC 13: C++23 15/15 in 6.78 s; C++20 15/15 in 7.19 s; all topologies and runtime/API checks passed; telemetry 70/70, web 9/9, build passed. Evidence: `outputs/linux-container/portable-20260903T232235Z.log`. |
| Compose and affected images | `docker compose config --quiet`; build generator/transform/sink/telemetry | Compose passed; both distinct current image definitions and all four services built successfully. |
| Clean image rebuild | No-cache generator and telemetry build | Demo image rebuilt successfully. Telemetry reached locked `npm ci --omit=dev` but produced no output for more than 90 seconds while the registry advisory endpoint was timing out, so the extra no-cache attempt was interrupted; the ordinary current-image build had already passed. |
| Container contents and identity | Image inspection plus production dependency check | Demo user `65532:65532`; telemetry user `node`; `capture-files.mjs` present; AJV dev dependency absent from production image. |
| Capture-volume hardening | `bash scripts/test-container-hardening.sh` | Passed writable-native/read-only-telemetry ownership checks in both initialization orders. |
| Isolated authenticated Compose runtime | Dedicated `graphx-phase9-verifier` project with 4096/1048576/5 and catalog 2/3 | No token and control token returned 401; observation token returned two sorted DLT-147 files, scanned 3, truncated true; omitted file downloaded and decoded as exactly five GraphX packets; all four services running; project/volume removed. |
| Independent parser/catalog boundary probe | 256 KiB and 256 KiB + 4 SHBs; 5,002-entry directory with limits 8/32 | Maximum SHB accepted, over-maximum rejected; 8 returned, 32 scanned, truncation true, 0.651 ms. |
| Documentation/configuration comparison | README, capture, observability, security, protocol/test docs, example guide, ADR, schema, YAML, Compose | Claims and default/range values matched code and observed API behavior. |
| Dependency trees and lockfile installs | `npm ls --omit=dev --all`; portable `npm ci` | Passed. Optional native accelerators/type packages were correctly reported as optional, not required. |
| Dependency advisories | `npm audit --omit=dev --fetch-timeout=20000 --fetch-retries=1` | Both telemetry and web advisory requests timed out at `https://registry.npmjs.org/-/npm/v1/security/advisories/bulk`; no current audit conclusion is claimed. |
| Syntax/schema/whitespace | Shell syntax, Node syntax, JSON parse, `git diff --check` | Passed. |

## 6. Unverified areas and why

- Native privileged macvlan, ipvlan, network-namespace, nftables, and Open
  vSwitch laboratories were not rerun because this host is macOS and the local
  Linux container does not provide the required host networking topology and
  privileges. Those are earlier-phase network features, not the Phase 9
  application-PCAPNG boundary. Portable Linux and Compose behavior was verified.
- A live physical OVS SPAN Ethernet capture was not produced for the same host
  limitation. The DLT-1 writer/extcap representation and checked-in OVS capture
  integration were inspected and tested, but a native Linux lab remains the
  appropriate environment for end-to-end mirrored packets.
- Current vulnerability status could not be obtained because both explicit npm
  advisory requests timed out. Lockfiles, clean installs, dependency trees,
  tests, and current images were otherwise verified.
- Wireshark GUI interaction was not automated. The same Lua dissector and extcap
  protocol were exercised with real host and Ubuntu TShark plus extcap fixtures.

## 7. Compatibility and security assessment

The implementation preserves envelope versions 1 and 2, the four-byte
big-endian frame prefix, transport contracts, and configuration version 1.
Application capture observes canonical bytes above the transport boundary and
therefore remains independent of TCP, Unix sockets, shared memory, in-process
placement, Docker, and the GUI. USER0 is explicitly documented as private-use;
Ethernet mirror captures remain DLT 1.

Security boundaries are appropriate for the work package. Capture filenames
use a strict allowlist. Writer and collector opens use no-follow/nonblocking
semantics and descriptor validation. Downloads retain observation authorization
and cannot be authorized with a control token. Raw payload sensitivity,
retention responsibility, parent-directory trust, USER0 conflicts, and partial
storage-failure behavior are documented. Containers are non-root, capability
dropped, read-only, PID bounded, and use the intended read-write/read-only
capture-volume split.

Resource use is explicit and finite across writer files/packets/snapshots,
metadata comments, telemetry catalog entries/files/header blocks, extcap block
buffers, HTTP limits, and dissector lengths/counts. Reaching a capture limit did
not stop the live graph. No credential or payload logging regression was found.

## 8. Required remediation before acceptance

None. The P3 items in Section 4 are recommended maintenance improvements and do
not invalidate the current Phase 9 behavior. Rerun production dependency audits
when the npm advisory endpoint is reachable and retain that evidence for the
release gate.

## 9. Readiness for the next work package

The project is ready for **Phase 10: release engineering, compatibility policy,
packaging, and support processes**.

Phase 10 must preserve these invariants:

- captured application bytes remain the exact canonical framed envelope;
- envelope v1/v2 and configuration v1 compatibility rules remain explicit;
- packet, byte, metadata, parser, and catalog bounds cannot become optional;
- capture errors remain isolated from graph processing;
- raw captures remain observation-authorized sensitive data;
- USER0 conflicts and supported Wireshark versions remain explicit;
- release artifacts exercise the real writer/extcap/TShark fixtures; and
- packaging must not make GraphX semantics depend on Docker, a telemetry
  vendor, a GUI framework, or Wireshark being installed on runtime nodes.
