# Phase 9 implementation handoff

Date: 2026-09-03

Work package: PCAPNG, Wireshark dissector, and extcap implementation

## 1. Outcome summary

Phase 9 is implemented. GraphX now writes bounded, correlated PCAPNG files for
canonical application frames and Ethernet mirror packets; supplies a real Lua
dissector for GraphX envelope versions 1 and 2; and supplies a validating,
bounded extcap adapter that can copy a completed capture or follow an
append-only file while emitting only complete PCAPNG blocks.

The application capture representation remains the exact bytes already used on
GraphX transports: a four-byte big-endian envelope length followed by the `GXE`
envelope. Each record is one PCAPNG Enhanced Packet Block on private-use
LINKTYPE_USER0 (147). No GraphX wire version, framing rule, transport contract,
or configuration version changed. Actual Ethernet mirror files remain separate
and use LINKTYPE_ETHERNET (1), so Wireshark's standard protocol stack handles
them.

Capture resource use is now explicit and finite. Snap length, maximum file
bytes, and maximum packet count are validated in YAML, JSON schema, native
environment projection, telemetry environment projection, and Compose. The
writer opens without truncation and in nonblocking/no-follow mode, requires a
single-link regular file, and only then truncates and sets mode `0660`. It
therefore rejects symlinks, hard links, FIFOs, sockets, and devices before they
can block or lose data. Blocks are written through a checked descriptor and a
partial write is rolled back where possible. Optional correlation comments are
bounded separately from valid envelope size; oversized metadata produces an
explicit JSON truncation marker while the exact frame is still captured. A
runtime limit or I/O error disables capture only. Rotation remains explicit.

The telemetry service reports the effective limits, lists only single-link
regular PCAPNG files, opens each candidate once with nonblocking/no-follow
semantics, validates type, link count, size, section profile, and link type on
that descriptor, and streams the same descriptor. Raw captures are documented
as sensitive data and retain the existing observation authorization boundary.
Catalog work and response size are now bounded independently by maximum-file
and maximum-entry deployment limits. The service scans through a directory
handle, validates no more than the configured bounds, returns bytewise-sorted
results with scanned/truncated metadata, and caches the catalog for one second.
Direct authorized download remains available for a valid file omitted from a
truncated catalog.

The final Phase 9 verification findings are also remediated. JSON Schema Draft
2020-12 conditionals now match native and telemetry semantics: enabled capture
requires an explicit provider, and enabled `pcapng` requires a directory. An
AJV differential suite exercises the full checked-in schema. Initial PCAPNG
section/interface parsing reads complete bounded blocks rather than assuming
both fit in 512 bytes. The configuration CTest has a 30-second deadline and
prints each case immediately so a future stall terminates with useful evidence.

The original byte-blind shell tailer was replaced with a Python 3 extcap
implementation. It validates PCAPNG version 1.0, the little-endian byte-order
magic, indefinite section length, one initial Interface Description Block,
selected DLT, interface-zero packet ordering, block size, and trailing block
length before forwarding. It rejects later sections/interfaces and unknown
blocks. It holds at most one bounded block in memory, supports Wireshark
metadata/configuration calls, rejects unsupported capture filters, and rejects
non-regular or symlink sources using `O_NOFOLLOW`.

The Lua dissector registers itself for USER0, safely decodes both supported
envelope versions and all bounded strings/attributes/payloads, enforces nonzero
mandatory v2 message/trace identities and unique raw attribute keys while
allowing an absent zero parent, exposes `graphx.*` display-filter fields, and
reports malformed or unsupported packets with expert diagnostics. Its tests use
captures produced by the real C++ writer and a real TShark process on macOS and
Ubuntu 24.04/Wireshark 4.2. The Linux
verifier explicitly drops from root for this test because Wireshark disables Lua
when run as root.

No Phase 10 release packaging or support-policy work was implemented. The
pre-existing user edits to `prompt/implement.md` and `prompt/verifier.md` were
preserved and are not part of Phase 9.

## 2. Requirements implemented

| Requirement | Implementation evidence | Result |
|---|---|---|
| Valid PCAPNG application capture | `src/capture.cpp` writes SHB, IDB, nanosecond-resolution EPBs and exact canonical frames on USER0 | Met |
| Real Ethernet capture representation | `EthernetPcapngCaptureSink` writes actual Ethernet bytes on DLT 1, distinct from application frames | Met |
| Finite disk and record use | `snaplen`, `max_file_bytes`, and `max_packets` are enforced before a complete block is committed | Met |
| Safe exceptional paths | Descriptor RAII, nonblocking/no-follow open before truncation, single-link regular-file validation, checked writes, best-effort partial-block rollback, and capture-only disable on error | Met |
| Correlation | Packet comments retain edge, direction, sequence, wire version, message/parent/trace IDs, and type | Met |
| Wireshark protocol decoding | `wireshark/graphx.lua` decodes v1/v2 headers, identities, attributes, type, trace and payload with display filters | Met |
| Malformed-input handling | Dissector validates every read, exact frame length, supported versions, mandatory identities, unique attribute keys, 16 MiB envelope bound, 4,096 attributes, and no trailing bytes | Met |
| Functional extcap | `tools/graphx-extcap` implements interface, DLT, configuration, version/filter validation and capture operations | Met |
| Safe live follow | Extcap polls only for missing bytes, forwards only complete validated blocks, remains bounded, and handles cancellation/broken pipes | Met |
| Secure download boundary | Telemetry opens once, rejects symlink/non-regular/multiply linked objects, checks size and PCAPNG profile/DLT on that descriptor, and streams it without pathname reopen | Met |
| Configuration/deployment consistency | Typed loader, schema, sample YAML, native applications, telemetry and all Compose services use the same limits | Met |
| Bounded telemetry catalog | Directory entries inspected and files returned are independently bounded, results are cached and sorted, and truncation/scanned metadata is explicit | Met |
| Schema/runtime semantic equivalence | Draft 2020-12 capture conditionals require provider and the enabled-PCAPNG directory exactly where native and telemetry loaders do | Met |
| Automated runtime validation | CTest generates real v1/v2 captures, runs extcap, and conditionally runs real TShark; CI and Linux verifier install TShark | Met |
| Documentation and decision record | README, capture/observability/protocol/security/test docs, example docs, Wireshark guide, and ADR 0010 match behavior | Met |
| Phase isolation | No release packaging, signed artifacts, compatibility-support process, or other Phase 10 behavior was added | Met |

### Verification-report remediation traceability

| Verification finding | Remediation evidence | Regression evidence | Status |
|---|---|---|---|
| P1: writer blocks on FIFO and truncates multiply linked files before validation | `src/capture.cpp` opens with `O_NONBLOCK`, `O_NOFOLLOW`, and no `O_TRUNC`; it requires `S_ISREG` and `st_nlink == 1` before `ftruncate`/`fchmod` | `tests/test_main.cpp` covers normal existing files plus symlink, hard link, FIFO, Unix socket, device, preservation, and bounded return | Remediated |
| P2: capture types/providers differ across schema, YAML, and environment projection | Strict capture-specific YAML tag/type readers in `src/config.cpp`; provider enum in schema; strict native/telemetry environment parsers; explicit `pcapng` and established external `ovs-span` providers | `tests/test_config.cpp`, `tests/capture_runtime_config.cpp`, and `apps/telemetry/capture.test.mjs` reject quoted scalars, typo providers, and invalid booleans | Remediated |
| P2: Lua accepts mandatory zero IDs and duplicate keys | `wireshark/graphx.lua` checks mandatory message/trace IDs and tracks raw keys; zero parent remains allowed | `scripts/test-wireshark.sh` mutates the real C++ fixture for zero message, zero trace, zero parent, and duplicate key cases | Remediated |
| P2: extcap accepts unsupported section versions/profile changes | `tools/graphx-extcap` requires SHB 1.0, indefinite section length, one IDB, EPBs only, and interface ID zero | `tests/test_extcap.py` covers 0.9, 1.1, 2.0, bad length, short SHB, packet-before-IDB, extra SHB/IDB, wrong interface, unknown blocks, and a nonblocking FIFO-source rejection | Remediated |
| P2: valid large metadata disables capture | `src/capture.cpp` constructs comments within the PCAPNG option limit and falls back to deterministic valid JSON with `metadata_truncated:true` | `tests/test_main.cpp` records two frames with a 70,000-byte type and verifies both packets and the fallback marker | Remediated |
| P2: telemetry validates a different pathname open than it serves | `apps/telemetry/capture-files.mjs` validates header/DLT with positioned reads on the exact open descriptor returned for streaming | `apps/telemetry/capture.test.mjs` renames/replaces the pathname after open and verifies the original descriptor; it also covers hard links and FIFO nonblocking behavior | Remediated |
| P2: schema accepts enabled capture without the provider required by native code | Capture `if`/`then` conditionals in `config/schema/graphx.schema.json` require `provider` when enabled and `directory` for enabled `pcapng`; native and telemetry loaders use the same rule | `apps/telemetry/config-schema.test.mjs` validates complete documents with AJV 2020 and compares positive/negative cases with native and telemetry tests | Remediated |
| P2: synchronous telemetry snapshots scan and return an unbounded directory | `listValidatedCaptures` uses a directory iterator plus `catalogMaxEntries`/`catalogMaxFiles`; the server caches results for one second and reports truncation/scanned counts | Unit and live-service tests create larger catalogs, verify sorted bounded output, and download a valid file omitted from the list; a 20,000-file live probe returned 8 files after inspecting 32 entries in about 0.003 seconds | Remediated |
| P3: a Linux configuration CTest can hang without a deadline | `graphx-config-tests` has a CTest `TIMEOUT` of 30 seconds and its case output is unbuffered | Complete host, Linux C++20/C++23, portable, and sanitizer suites passed with the deadline active | Remediated |
| P3: telemetry assumes SHB plus IDB fit in the first 512 bytes | Descriptor parser reads and validates complete positioned SHB/IDB blocks with a 256 KiB initial-block ceiling, file-bound checks, and matching block trailers | Telemetry regression accepts a valid initial PCAPNG profile larger than 512 bytes and retains malformed/oversize rejection coverage | Remediated |

## 3. Architecture and compatibility decisions

### Capture ownership and failure semantics

`PcapngCaptureSink` remains a transport-neutral C++ observer. Applications own
one sink per process and record the same canonical frame regardless of whether
the runtime edge is TCP, Unix-domain socket, shared memory, or in-process. The
sink owns one file descriptor and serializes records with a mutex. A packet is
counted and its offset published only after the whole block is written.

The three limits have these defaults and validation ranges:

| Setting | Default | Accepted range |
|---|---:|---:|
| `snaplen` / `GRAPHX_CAPTURE_SNAPLEN` | 16,777,220 | 256–16,777,220 bytes |
| `max_file_bytes` / `GRAPHX_CAPTURE_MAX_FILE_BYTES` | 268,435,456 | 65,536–4,294,967,296 bytes |
| `max_packets` / `GRAPHX_CAPTURE_MAX_PACKETS` | 1,000,000 | 1–100,000,000 packets |

The minimum file bound always accommodates headers. The maximum snap length is
the 16 MiB GraphX envelope bound plus the four-byte frame prefix. On a limit or
write failure, the application logs a structured capture error and releases the
capture sink; graph processing and other telemetry continue. Rotation and
retention are deliberately operator-owned rather than implicit.

### File representation

Application capture uses USER0 because records are GraphX application frames,
not fabricated Ethernet/IP/TCP packets. The PCAPNG interface name and
description make that distinction explicit. The decision preserves exact wire
bytes and avoids inventing network addresses or checksums. USER0 is private-use,
so GraphX must use a dedicated Wireshark profile when another local protocol
also claims it.

Ethernet mirror capture is a separate abstraction and DLT. OVS mirror scripts
continue to produce standard network traffic; Phase 9 does not route those
bytes through the GraphX envelope dissector.

### Wireshark integration

Lua was selected over a native Wireshark plugin to avoid a version-specific C
ABI and additional build system while keeping the parser reviewable. The
dissector replaces Wireshark's generic USER0 handler only when explicitly
loaded. It registers filterable fields for version, sequence, nanosecond
timestamp, message/trace/parent identities, type, attribute lengths/keys/values,
and payload. Unknown versions and structural failures use expert diagnostics;
they are not presented as successfully decoded GraphX messages.

### Extcap integration

The extcap process is a block validator and bridge, not another capture engine.
The source is an operator-selected GraphX PCAPNG file and the destination FIFO
is created by Wireshark. `--no-follow` reads the current file and exits; the
default waits for appended complete blocks. Capture filters are not meaningful
for already-produced PCAPNG and are explicitly rejected in favor of Wireshark
display filters.

The adapter accepts only the little-endian PCAPNG 1.0 profile produced by
GraphX, requires an indefinite section followed by exactly one IDB, accepts
only interface-zero EPBs after that, caps a block at 17 MiB, and validates the
selected application or Ethernet DLT. It never parses arbitrary packet payload
content and never controls a GraphX runtime.

### Compatibility

- GraphX configuration remains version 1; capture keys are additive and have
  defaults.
- Envelope versions 1 and 2 and the four-byte framing prefix are unchanged.
- Existing capture APIs remain source-compatible through constructor defaults;
  byte and packet counters are additive.
- Existing telemetry capture routes remain observation-authorized. The API adds
  effective limits but does not remove prior fields.
- Python 3 is the only extcap runtime dependency. TShark/Wireshark is optional
  for a normal GraphX build but installed in Linux verification/CI so runtime
  dissector validation cannot silently disappear there.

ADR 0010 records these decisions and the rejected direction of fabricating
network packets or binding the project to Wireshark's native plugin ABI.

## 4. Files and major components changed

- Capture writer/API: `include/graphx/capture.hpp`, `src/capture.cpp`.
- Configuration and projections: `include/graphx/config.hpp`, `src/config.cpp`,
  `config/schema/graphx.schema.json`, `graphx.yaml`, `apps/common.hpp`,
  `apps/cli/main.cpp`, and `compose.yaml`.
- Collector download boundary: `apps/telemetry/server.mjs`,
  `apps/telemetry/capture-files.mjs`, and `apps/telemetry/capture.test.mjs`.
- Wireshark tooling: `wireshark/graphx.lua`, `wireshark/README.md`, and
  `tools/graphx-extcap`.
- Native/runtime tests: `tests/capture_fixture.cpp`, `tests/test_extcap.py`,
  `scripts/test-wireshark.sh`, `tests/test_main.cpp`, `tests/test_config.cpp`,
  `tests/capture_runtime_config.cpp`, and `CMakeLists.txt`.
- CI/verifier: `.github/workflows/ci.yml` and
  `docker/linux-verifier.Dockerfile`.
- Documentation: `README.md`, `docs/capture.md`, `docs/observability.md`,
  `docs/protocol.md`, `docs/security.md`, `docs/test-procedure.md`,
  `examples/capture/README.md`, and
  `docs/adr/0010-bounded-pcapng-wireshark-extcap.md`.

## 5. Tests and checks run

All results below were run during the final remediation on 2026-09-03. The
complete host, Linux C++20/C++23, sanitizer, quality/fuzz, telemetry, web,
Compose build, hardening, and isolated runtime boundaries were rerun after the
schema, catalog, parser, and CTest-deadline changes.

| Check | Command/evidence | Exact result |
|---|---|---|
| Fresh macOS C++23 build | Fresh build directory; CMake, Ninja, complete CTest | 15/15 passed in 6.40 s; real local TShark included |
| Sanitizers | Fresh sanitizer build with supported macOS ASan/UBSan options and complete CTest | 16/16 passed in 11.52 s, including sanitizer-coverage guard |
| Exact Linux CTest | `scripts/test-linux-container.sh ctest` | Ubuntu 24.04/GCC 13: C++23 15/15 in 6.91 s and C++20 15/15 in 4.81 s; TShark 4.2 included. Evidence: `outputs/linux-container/ctest-20260903T230504Z.log` |
| Linux portable acceptance | `scripts/test-linux-container.sh portable` | Passed; C++23 and C++20 each 15/15, every topology, TCP/shared-memory/lifecycle/control/HTTPS checks, telemetry 70/70, web 9/9, and production build. Evidence: `outputs/linux-container/portable-20260903T230844Z.log` |
| Exact quality gate | `GRAPHX_FUZZ_SECONDS=5 scripts/test-linux-container.sh quality` | clang-format-18 passed 39 files; clang-tidy-18 and cppcheck passed all targets; sanitizer coverage passed; both libFuzzer targets passed five seconds. Evidence: `outputs/linux-container/quality-20260903T230549Z.log` |
| Focused capture tools | CTest regex `graphx-(extcap|wireshark-dissector)` | 2/2 passed on macOS; Linux tests also passed in both language modes |
| Extcap adversarial cases | `tests/test_extcap.py` through CTest | Copy/live follow passed; DLT/filter/truncation/size/trailer/symlink/FIFO cases and unsupported SHB versions, ordering, extra interfaces/sections, interface ID, and unknown blocks were rejected |
| Dissector adversarial cases | `scripts/test-wireshark.sh` through CTest | v1/v2 fields and filters passed; structural errors, mandatory zero IDs, and duplicate keys produced expert diagnostics; zero parent remained valid |
| Writer adversarial cases | `tests/test_main.cpp` through CTest | Existing regular file handled; symlink, hard link, FIFO, socket, and device rejected promptly and preserved; two valid 70,000-byte-type records captured with bounded metadata marker |
| Configuration projections | Native loader tests, `graphx-capture-runtime-config`, and telemetry startup tests | Quoted YAML capture types, invalid environment booleans, and unsupported providers rejected; case-insensitive valid booleans and `pcapng`/`ovs-span` accepted |
| Telemetry capture boundary | `npm test --prefix apps/telemetry` | 70/70 passed repeatedly; includes descriptor/path replacement, hard-link/FIFO/unknown-DLT rejection, typed startup, bounded catalog, large initial blocks, schema differential cases, limits, valid download, and symlink rejection |
| Web | `npm test --prefix web`, `npm run build --prefix web` via portable suite | 9/9 passed; Vite production build passed. Existing large-chunk warning remains non-fatal |
| Dependency audit | `npm audit --omit=dev --fetch-timeout=10000 --fetch-retries=0` in telemetry and web | Web reported 0 vulnerabilities. Telemetry's advisory request timed out after 10 seconds, so no current telemetry vulnerability conclusion is claimed. |
| Syntax/configuration | Python compile, Node syntax checks, JSON-schema parse, `docker compose config --quiet`, `git diff --check` | Passed |
| Affected images | `docker compose build generator transform sink telemetry` | All four service builds passed |
| Container capture-volume permissions | `bash scripts/test-container-hardening.sh` | Passed in both fresh-volume initialization orders |
| Isolated Compose runtime | Dedicated `graphx-phase9-remediation` project and new temporary volume | API reported 4096/1048576/5 capture limits, 8/32 catalog limits, three USER0 files, three entries scanned and no truncation; each file contained exactly five GraphX packets; all four services remained up; project and volume removed |

During the initial implementation, the first Linux portable run exposed that Wireshark 4.2
used its generic USER0 handler and that container-root execution disabled Lua.
The registration now replaces the generic handler, and the test runner executes
TShark as an unprivileged user when the surrounding verifier is root. A first
retest then exposed the temporary directory's restrictive mode; the runner now
grants traversal only for that isolated directory and keeps capture files
read-only to the test user. The final Linux C++20/C++23 and portable results
above prove both remediations.

The independent verifier then found the six filesystem/configuration/parser
boundaries traced in Section 2. The remediated suite reproduces each condition
as a regression test. The first remediation quality run correctly failed on
formatting in `src/capture.cpp`; the file was formatted with the pinned
`clang-format-18`, and the complete quality gate then passed. Two early isolated
runtime assertions expected the decoded byte field as text and searched for an
incorrect log phrase; both isolated projects were cleaned automatically. The
corrected run checked the hex byte field, exact packet counts, and live service
state and passed.

The subsequent Phase 9 verifier found the four schema/catalog/deadline/header
boundaries also traced in Section 2. All four now have regression coverage. The
first final Linux portable rerun passed both CTest modes, telemetry, web, and
the runtime control checks, then exposed a stale secure-start probe that did not
supply the newly required PCAPNG directory. Both secure and insecure-bind
probes now provide that directory, ensuring the latter still tests the bind
policy rather than failing earlier for unrelated configuration. The complete
portable suite then passed. An initial final quality rerun also correctly found
format drift in the two changed C++ files; pinned `clang-format-18` corrected
them and the entire quality gate was rerun successfully.

The host does not provide `clang-format-18`; the host wrapper correctly refused
to substitute another version. The required exact-version check was not
skipped: it passed inside the pinned Ubuntu verifier image.

## 6. Known limitations

1. USER0 is private-use rather than a globally assigned GraphX link type. Use a
   dedicated profile and do not install a competing USER0 dissector there.
2. Extcap follows one append-only file. It does not follow filename rotation,
   reconnect to a replacement inode, capture directly from a socket, or provide
   remote authentication.
3. GraphX intentionally stops capture at a limit. Operators must export,
   archive, rotate, and delete sensitive captures according to local policy.
4. The extcap reader accepts only GraphX's little-endian PCAPNG layout and one
   interface. This is deliberate validation, not a general PCAPNG library.
5. Lua parsing is runtime-tested with the locally installed Wireshark and Ubuntu
   24.04's Wireshark 4.2. Release packaging and a formal supported-version
   matrix belong to Phase 10.
6. Captures contain raw message types, attributes and payloads. They are not
   encrypted or redacted at rest and must be handled as sensitive evidence.
7. Output validation protects the final path component and multiply linked
   inodes. It does not claim that the configured parent directory is safe from
   replacement by an actor with permission to rename that directory; deployments
   must keep capture-directory ownership and mount boundaries trusted.
8. A storage device can still fail in a way that prevents best-effort rollback
   after a partial OS write. Readers reject incomplete blocks; operators should
   treat such a file as ending at the last complete block.
9. The telemetry remediation-time npm advisory request timed out. Lockfile
   installation, tests, and image builds passed, and the web audit reported
   zero production vulnerabilities, but a release gate must rerun the telemetry
   audit when the registry advisory endpoint is available.
10. Catalog enumeration is intentionally synchronous but finite and cached.
    Defaults inspect at most 512 entries and return at most 128 validated files;
    deployments may choose smaller or larger values only within documented
    ceilings. A truncated catalog is not a complete inventory, although direct
    authorized retrieval of a known valid capture remains available.

## 7. Risks for independent verification

The verifier should examine these boundaries closely:

- Parse PCAPNG independently and compare EPB payloads byte-for-byte with the
  canonical C++ frame, including snap truncation and comment escaping.
- Exercise file-byte and packet limits concurrently and confirm graph traffic
  continues after capture disables itself.
- Attempt symlink swaps, hard links, FIFOs, sockets, devices, and other
  non-regular files at writer, extcap, listing, and download boundaries;
  confirm bounded return, rejected-target preservation, and that no data outside
  the capture directory is read.
- Feed extcap malformed block lengths, trailers, ordering, byte-order magic,
  DLTs and a file that ends mid-block while following; observe bounded memory
  and clean cancellation.
- Feed the Lua dissector zero/oversized/mismatched frame lengths, mandatory zero
  and truncated v2 identities, zero parent, duplicate/binary keys, extreme
  attribute counts, unknown versions, and trailing bytes under each supported
  Wireshark version.
- Confirm observation authorization still protects capture list/download when
  enabled and that Phase 8 control credentials are not accepted as observation
  credentials by accident.
- Verify a real Linux Docker volume initializes with writer group 65532 and
  collector read-only access in both startup orders; existing CI covers this
  identity contract.
- Populate more entries than both catalog limits, verify bounded sorted output
  and explicit truncation metadata, and confirm a known omitted capture can
  still be downloaded without increasing listing work.
- Compare full-document Draft 2020-12 schema results with native and telemetry
  loaders for enabled/disabled, `pcapng`/`ovs-span`, missing provider/directory,
  and quoted scalar cases.
- Check that CI cannot silently skip the real dissector test on Linux after an
  image/package change and that TShark never runs Lua as root.

## 8. Acceptance-criteria checklist

- [x] Exact canonical application frames are written as valid PCAPNG USER0
      packet records with nanosecond timestamps and correlation comments.
- [x] Actual Ethernet packet capture remains distinct on LINKTYPE_ETHERNET.
- [x] Capture snap length, file bytes, and packet count are finite,
      configurable, consistently projected, and negatively tested.
- [x] Capture listing work and response size are finite, cached, independently
      configurable, deterministically sorted, and report truncation/scanned
      metadata without blocking direct authorized retrieval.
- [x] Full-document JSON Schema, native, and telemetry capture conditionals
      agree for enabled provider and enabled-PCAPNG directory requirements.
- [x] Capture errors stop capture without stopping the graph or collapsing
      transport outcomes.
- [x] Output and download paths reject symlink, hard-link, and special-file
      attacks before mutation or blocking and validate the descriptor served.
- [x] The Lua dissector decodes both supported GraphX wire versions and exposes
      useful display filters.
- [x] Malformed/truncated/oversized/unknown/trailing envelope data is bounded and
      diagnosed rather than trusted.
- [x] Extcap implements the required Wireshark discovery/configuration/capture
      protocol and validates PCAPNG before forwarding.
- [x] Live follow forwards complete blocks only and handles cancellation without
      an unbounded buffer.
- [x] Tests invoke the real writer, extcap and TShark on macOS and Linux rather
      than merely inspecting source text.
- [x] README, operator/security/protocol/test documentation and ADR match the
      implementation and its limits.
- [x] Fresh native, sanitizer, exact static-analysis/format, portable,
      lockfile installation, Compose build and isolated runtime checks pass.
- [ ] Rerun the telemetry production dependency audit when the npm advisory
      endpoint is available; its remediation-time request timed out. The web
      audit reported zero vulnerabilities.
- [x] No GraphX wire-format or configuration-version migration was introduced.
- [x] No Phase 10 implementation was introduced.

## 9. Recommended next work package

The implementation is ready for independent Phase 9 re-verification. Do not
begin the next work package until that verifier returns `ACCEPTED`. If accepted,
Phase 10 is release engineering, compatibility policy, packaging, and support
processes. It should preserve these Phase 9 invariants:

- the captured application payload is the exact canonical framed envelope;
- v1 and v2 remain decoded according to the documented protocol;
- capture and extcap resource bounds cannot become optional or implicit;
- raw captures remain observation-authorized sensitive data;
- USER0 conflicts and supported Wireshark versions are explicit;
- release packages must run the same real-writer/extcap/TShark fixtures used by
  CTest; and
- packaging must not make GraphX semantics depend on Docker, a telemetry vendor,
  a GUI framework, or Wireshark being installed on runtime nodes.
