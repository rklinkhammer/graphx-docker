# Phase 11 independent verification: UDP edges

Date: 2026-09-04  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Baseline commit: `1cbbfd542fde41c67bd0c561c5aa5d67aff4cbd5` (`install certs`)  
Project version: `1.0.0`

## 1. Verdict

**BLOCKED**

All Phase 11 requirements that can be executed safely on the available macOS/OrbStack host pass.
The two findings from the preceding verification are independently confirmed fixed:

- sustained malformed UDP traffic no longer extends a finite receive deadline; and
- the portable feature suite no longer leaks `GRAPHX_MAX_MESSAGES=0` into UDP examples.

No P0, P1, or P2 implementation finding remains in this pass. Phase 11 cannot receive an
`ACCEPTED` verdict because UDP-003 explicitly requires an isolated **native Linux** broadcast
acceptance run. The available host is macOS. Docker/OrbStack is excluded as substitute evidence
by `prompt/verifier.md`, and an additional privileged-container capability probe failed with
`mkdir /run/netns failed: Permission denied`.

Run the documented native namespace gate on a suitable Linux host, including interruption and
idempotent teardown. If that passes without a new substantive finding, Phase 11 can move from
`BLOCKED` to `ACCEPTED`.

## 2. Executive summary

Phase 11 implements bounded IPv4 UDP unicast, broadcast, and multicast edges without changing
the GraphX configuration version or envelope wire versions. Each datagram contains exactly one
existing four-byte `u32be` frame and one complete envelope. UDP exposes no session, retry,
acknowledgement, TLS, backpressure, end-of-stream, or delivery guarantee.

The final implementation passed:

- a fresh macOS C++20 build and all 24 CTests;
- 20 consecutive executions of each UDP transport/unicast/multicast test;
- 100 consecutive complete lifecycle-stress executions;
- an independent eight-thread malformed-flood probe, returning at exactly 50 ms;
- an independent allocation and descriptor probe;
- Linux C++20 and C++23 complete suites;
- the full Linux portable feature tier, including all topologies and examples;
- Clang 18 ASan/UBSan, format, clang-tidy, cppcheck, and fuzz gates;
- release contract, staged package installation, and external consumer compilation;
- telemetry 71/71, web 9/9, production web build, runtime control, and HTTPS checks;
- current-image isolated Docker broadcast delivery of 5/5 datagrams and teardown; and
- a real five-packet Docker-network capture decoded by the checked-in Lua dissector as GraphX
  version 2, sequences 1 through 5.

The implementation stays within Phase 11. No SDR topology, IPv6, reliable UDP, DTLS,
fragmentation/reassembly, or native one-to-many graph model was introduced.

## 3. Environment and repository state

### Host and tools

- Host: macOS 26.6.2, Darwin build 25G83, arm64.
- Native compiler: Apple Clang 21.0.0; CMake 4.4.3; OpenSSL 3.6.3.
- Docker client/server: 29.4.0; Linux arm64 server under OrbStack.
- Linux verifier: Ubuntu 24.04 arm64, GCC 13.3, Clang/LLVM 18.1.3,
  OpenSSL 3.0.13, Node 24.20.0, npm 11.19.0.
- TShark: 4.6.8.
- Native Linux namespace/veth/bridge behavior: unavailable because the physical host is macOS.
- ThreadSanitizer is not configured; no TSan success is claimed.

### Repository state and scope

The worktree remains intentionally dirty with the Phase 11 implementation: 28 modified tracked
path entries and 11 untracked top-level path entries at inspection. Changes cover UDP transport,
configuration, factory/CLI integration, observability, applications, tests, examples, capture,
packaging, documentation, and the Phase 11 work packages. `git diff --check` passed.

No SDR/radio node, switch topology, IPv6, reliability layer, DTLS, application fragmentation, or
one-to-many graph data-model extension was found. Verification made no product-code change. This
report is the only repository file replaced by the verifier; temporary probe artifacts live
outside the repository.

## 4. UDP-001 through UDP-013 traceability

| ID | Requirement | Implementation evidence | Independent runtime evidence | Status | Remediation |
|---|---|---|---|---|---|
| UDP-001 | Strict configuration for all three modes | `include/graphx/config.hpp`; `src/config.cpp`; JSON schema; CLI; factory | Complete config tests passed in fresh native and Linux C++20/C++23 suites. Inspected cases cover missing/unknown/wrong-type/empty values, malformed IPv4, mode mismatch, ports 0/1/65535/65536, TTL 0/255/overflow, buffer/datagram boundaries and overflow, framing, TCP-only keys, orphan entries, and kind mismatch. All three YAML examples validated. Unavailable interface produces actionable runtime error. | **Passed** | None. |
| UDP-002 | Factory-created loopback unicast with content and observability | `src/transport_factory.cpp`; `src/udp_transport.cpp`; public header | Factory round trip, content, timeout, state, datagram/byte, and sequence metrics passed. Unicast example passed 20 repetitions and the Linux portable tier. | **Passed** | None. |
| UDP-003 | Isolated native Linux broadcast without physical traffic | Mode-specific `SO_BROADCAST`; native runner creates two namespaces, veth pairs, and unattached bridge | Current-image internal Docker broadcast delivered 5/5 and cleaned up. Host native runner returned 2 because host is not Linux. Privileged OrbStack probe could not create `/run/netns`. Docker evidence cannot satisfy this identifier. | **Not verified** | Run `GRAPHX_VERIFY_LIVE_CAPTURE=1 examples/udp-broadcast/run-native-linux.sh` on native Linux; interrupt a setup run; run teardown twice; record absence of leftover namespaces/links. |
| UDP-004 | One multicast datagram reaches two joined listeners | Explicit membership join/leave, interface, TTL, loopback, and reuse options | Two listeners receive the same message ID; closing one does not affect the other; invalid interface is rejected. Multicast CTest/example passed 20 repetitions and Linux runs. | **Passed** | None for Phase 11. Validate routed multicast/IGMP per deployment. |
| UDP-005 | Whole-datagram integrity, bounds, deadline, and recovery | Fixed receive buffer; `recvmsg`/`MSG_TRUNC`; exact prefix check; deserializer; size preflight; absolute deadline at `src/udp_transport.cpp:406-490` | Raw empty/short/mismatch/bad-magic/unknown-version/trailing/truncated cases, exact maximum, plus-one rejection, and recovery passed. External eight-thread flood returned `timeout` in 50 ms while counting 5,407 malformed datagrams; checked regression passed native and Linux/sanitizer runs. | **Passed** | None. Retain adversarial regression. |
| UDP-006 | Timeout/cancel distinction and deterministic lifecycle | Cancellation socket; synchronized descriptor lifetime; idempotent close; move-only pImpl | Timeout/cancel, move/destruction, double close, concurrent close, and repeated construction passed. Lifecycle CTest passed 100 repetitions. Resource probe reported `fd_before=4 fd_after=4`. | **Passed** | P3: add supported TSan coverage when practical. |
| UDP-007 | UDP semantics only | Strict UDP allowlist and public API; UDP operations documentation | TCP-only settings rejected. No EOS path is exposed. Loss, duplication, reordering, lack of delivery/security guarantees, and sequence-estimate limitations are documented and controllable anomaly tests pass. | **Passed** | None. |
| UDP-008 | Complete counters and bounded diagnostics | Typed UDP events, fixed sequence window, default no-op callback, one-text-per-second socket limiter | Send/receive bytes/datagrams, malformed, truncated, oversize, socket error, gap, duplicate, and out-of-order assertions passed. Three repeated socket failures produced three counters and one diagnostic. Throwing observer did not affect processing. Flood counters remained bounded in memory and emitted no per-packet text. | **Passed** | None. |
| UDP-009 | Real UDP capture and correct dissector rejection | Lua UDP port preference/heuristic with exact framed-length check; generated capture regressions; optional native capture | Existing USER0/TCP/UDP and malformed-length tests passed. A new real isolated-network capture decoded destination `172.31.91.255`, port 47102, version 2, sequences 1-5. | **Passed** | Execute the already implemented native capture path with UDP-003. |
| UDP-010 | Three deterministic documented examples with cleanup | Shared publisher/subscriber; three YAML/README/run sets; offline preloaded-image broadcast | Unicast and multicast passed repeatedly and in portable tier. A newly built current broadcast image delivered 5/5 with `--no-build --pull never`; missing image returned 2 clearly; containers/network were removed. | **Passed** | Native acceptance remains tracked solely by UDP-003. |
| UDP-011 | Existing API, wire, transports, apps, and package remain compatible | Additive enum/header/factory/callback; unchanged config and envelope versions | Fresh full 24/24; golden/boundary/mixed-version tests; release contract; package install; external consumer; telemetry and web suites passed. | **Passed** | None. |
| UDP-012 | Clean build, complete tests, sanitizer/static/format/package/container gates | CMake, Docker verifier, release and feature scripts | Fresh clean native build 24/24; Linux C++20/23 24/24 each; full portable tier; Clang 18 ASan/UBSan; format/tidy/cppcheck; fuzz; package consumer; rebuilt runtime image all passed. TSan/native namespace exclusions are recorded. | **Passed with recorded limits** | Run native network tier on Linux. |
| UDP-013 | Accurate protocol, limits, operations, platform documentation | README; UDP guide; protocol/test procedure; ADR 0012; example and Wireshark READMEs | Inspection confirms IPv4 scope, exact framing, max size, MTU/IP fragmentation risk, no GraphX fragmentation, buffer clamping, firewall, IGMP, broadcast boundaries, Docker/macOS/Linux limitations, absolute deadline, spoofing, and lack of confidentiality/authentication. | **Passed** | None. |

## 5. Findings and blocker

No P0, P1, or P2 product finding remains.

### B-001 — Environmental blocker — UDP-003: native Linux namespace broadcast unavailable

- **Location:** required runtime environment; implementation entry point is
  `examples/udp-broadcast/run-native-linux.sh`.
- **Evidence:** on the macOS host the runner exits 2 with
  `Native UDP broadcast verification requires a Linux host`. A disposable privileged OrbStack
  container running `ip netns add gxudp-capability-probe` exits 1 with
  `mkdir /run/netns failed: Permission denied`.
- **Expected:** isolated directed broadcast is executed using native Linux namespaces, veth pairs,
  and a bridge that has no physical uplink; live capture proves sequences 1-5; interruption and
  repeated teardown leave no resources.
- **Actual:** implementation and scripts are available and pass inspection, but this environment
  cannot execute the required kernel namespace operation. Docker delivery succeeds but is
  explicitly excluded as substitute native evidence.
- **Impact:** UDP-003 remains not verified, so the verdict cannot be `ACCEPTED`.
- **Remediation:** run the documented command on a native Linux host with `ip`, `sudo`, `dumpcap`,
  and `tshark`; test normal completion, interruption, and teardown twice.
- **Required acceptance evidence:** listener `PASS received=5`, capture
  `PASS live-capture sequences=1-5`, zero exit, and no `gx-udp-*`, `gxudp-*`, or `gxudp-br`
  namespace/link artifacts after each cleanup.

### Non-blocking observations

- GCC 12 in the runtime-image Release build emits existing `-Wrestrict` optimizer warnings in
  `src/observability.cpp`; Clang 18 `-Werror` static analysis and ASan/UBSan pass. This warning is
  outside the Phase 11 change and is not classified as a UDP finding.
- Vite reports a non-failing 1.85 MB main-bundle size warning. It is unrelated to Phase 11.
- A first verifier-only portable invocation mounted `/workspace` read-only and failed at `npm ci`
  with `EROFS`. The documented writable invocation immediately afterward passed; the read-only
  result is not a product failure.

## 6. Exact tests and checks run

### Fresh native build and full regression

```sh
cmake -S . -B build/phase11-verifier-final-20260904 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGRAPHX_BUILD_TESTS=ON
cmake --build build/phase11-verifier-final-20260904 -j 4
ctest --test-dir build/phase11-verifier-final-20260904 --output-on-failure
```

Result: clean configure/build passed; **24/24 tests passed in 14.30 seconds**. This includes the
release contract and package install/external-consumer test.

### Timing and lifecycle repetition

```sh
ctest --test-dir build/phase11-verifier-final-20260904 \
  -R 'graphx-udp-(transport|unicast-example|multicast-example)' \
  --repeat until-fail:20 --output-on-failure
ctest --test-dir build/phase11-verifier-final-20260904 \
  -R '^graphx-transport-lifecycle-stress$' \
  --repeat until-fail:100 --output-on-failure
```

Result: all three UDP selections passed 20 times (**57.25 seconds**); complete lifecycle stress
passed 100 times (**60.23 seconds**).

### Independent adversarial probes

The verifier compiled two temporary programs outside the repository against the fresh static
library and ran them directly:

```text
malformed flood: status=1 elapsed_ms=50 malformed=5407
resource probe:  largest_send_allocation=128 fd_before=4 fd_after=4
```

`ReceiveStatus` value 1 is `timeout`. The flood used eight loopback senders for longer than the
requested 50 ms deadline. The resource probe attempted a 1 MiB envelope with a 256-byte UDP
limit and repeated 500 transport lifecycles.

### Linux portable acceptance

```sh
docker run --rm \
  -v "$PWD:/workspace" -v "$PWD/outputs/linux-container:/evidence" \
  -e GRAPHX_BUILD_JOBS=4 graphx-linux-verifier:local portable
```

Result: passed. Linux C++23 and C++20 each passed 24/24; all checked-in topologies validated;
TCP, signal shutdown, shared-memory, UDP unicast, and two-listener multicast examples passed;
telemetry passed 71/71; web passed 9/9 and built; authenticated controls and HTTPS passed.
Evidence: `outputs/linux-container/portable-20260904T193954Z.log`.

### Linux sanitizers and quality

```sh
docker run --rm -v "$PWD:/workspace:ro" \
  -v "$PWD/outputs/linux-container:/evidence" -e GRAPHX_BUILD_JOBS=4 \
  graphx-linux-verifier:local sanitizers
docker run --rm -v "$PWD:/workspace:ro" \
  -v "$PWD/outputs/linux-container:/evidence" -e GRAPHX_BUILD_JOBS=4 \
  -e GRAPHX_FUZZ_SECONDS=2 graphx-linux-verifier:local quality
```

Results:

- Clang 18 ASan/UBSan: all 24 enabled functional tests plus sanitizer coverage passed in 14.88
  seconds. Package is intentionally disabled for the instrumented static archive and passed in
  the clean native and both portable builds.
- `clang-format-18` checked 43 C++ files. Clang-tidy-18 and cppcheck passed every production,
  application, and test target. Both fuzz targets passed their two-second smoke runs.
- Evidence: `outputs/linux-container/sanitizers-20260904T194110Z.log` and
  `outputs/linux-container/quality-20260904T194110Z.log`.

### Current runtime image and isolated broadcast

```sh
docker build -t graphx-demo:phase11-verifier-final .
GRAPHX_BROADCAST_IMAGE=graphx-demo:phase11-verifier-final \
  examples/udp-broadcast/run.sh
```

Result: current image built; listener received sequences 1-5 and reported `PASS received=5`;
runner exited zero; Compose containers and internal network were absent afterward. With a
deliberately missing tag, the runner exited 2 before creating traffic and explained how to build
or load the image.

### Real PCAPNG and dissector

The verifier started only the broadcast listener on the internal Compose network, attached a
bounded `dumpcap` container to that network, sent the five beacons from the current image, and
decoded the PCAPNG using the checked-in `wireshark/graphx.lua`. TShark output was:

```text
172.31.91.4,172.31.91.255,47102,2,1
172.31.91.4,172.31.91.255,47102,2,2
172.31.91.4,172.31.91.255,47102,2,3
172.31.91.4,172.31.91.255,47102,2,4
172.31.91.4,172.31.91.255,47102,2,5
```

Capture and Compose resources were removed after the check.

### Native Linux capability check

```sh
GRAPHX_BUILD_DIR="$PWD/build/phase11-verifier-final-20260904" \
  examples/udp-broadcast/run-native-linux.sh
docker run --rm --privileged --entrypoint /bin/sh \
  graphx-demo:phase11-verifier-final \
  -c 'ip netns add gxudp-capability-probe && ip netns delete gxudp-capability-probe'
```

Results: host runner returned 2 because the host is Darwin; privileged OrbStack probe returned 1
because `/run/netns` could not be prepared. No native broadcast success is claimed.

## 7. Evidence classification

### Runtime verified

- Clean macOS C++20 build, complete CTest, package, release, and repeated tests.
- Independent malformed-flood and resource/descriptor probes.
- Linux-container C++20/C++23, complete portable tier, ASan/UBSan, Clang 18 format/tidy,
  cppcheck, and fuzzing.
- Loopback unicast and two-listener multicast.
- Current-image isolated Docker broadcast, missing-image failure, and teardown.
- Real Docker-network PCAPNG capture and TShark/Lua decode.
- Telemetry, web, runtime controls, and HTTPS integration.

### Inspection only

- Native broadcast namespace/veth/bridge setup, signal trap, cleanup, and optional capture logic.
- Physical/routed multicast operations and deployment firewall/IGMP behavior.
- No-SDR and one-logical-edge scope confirmation.

### Not verified

- Native Linux directed broadcast in namespaces (UDP-003).
- Native Linux interruption-during-setup and idempotent post-interruption cleanup.
- Native Linux optional live-capture path.
- ThreadSanitizer and hostile descriptor-reuse instrumentation; TSan is not configured.
- Physical Ethernet broadcast/multicast, intentionally excluded for safety.

## 8. Protocol, compatibility, security, and denial-of-service assessment

The implementation retains the existing `u32be` framing and envelope format. A receiver uses one
fixed bounded buffer and rejects truncation, outer-length disagreement, invalid envelopes,
unsupported versions, and trailing content before publishing a message. The sender uses a
non-allocating checked size before serialization and performs one `sendto` call.

The corrected receive loop uses one absolute deadline and checks it between datagrams even when
the socket remains readable. Cancellation is checked first and still returns a distinct outcome.
The independent flood and close/lifecycle repetitions support both properties. Malformed traffic
uses typed counters without per-packet error text; socket-error text is rate-limited while its
counter continues. Buffers, sequence history, allocation, logging, and label cardinality are
bounded.

UDP deliberately has no delivery, ordering, congestion-control, retry, acknowledgement,
backpressure, TLS, peer-liveness, or end-of-stream semantics. Sequence metrics are estimates.
Multicast remains one logical GraphX source-to-destination edge; the second listener is diagnostic
network fan-out only.

UDP source addresses are spoofable and group membership is not authorization. UDP provides no
confidentiality or authentication. Operators must enforce segmentation, firewalling, rate limits,
and application authentication as required. Error diagnostics contain edge/operation context,
not payloads or secrets.

## 9. Documentation and example assessment

README, the UDP guide, protocol reference, test procedure, ADR 0012, example READMEs, and
Wireshark README agree with the implementation. They cover IPv4-only scope, exact framing,
65,507-byte maximum, MTU/IP fragmentation risk, no GraphX fragmentation, absolute deadlines,
socket-buffer adjustment, firewall and broadcast boundaries, multicast/IGMP, interface selection,
platform limitations, spoofing, lack of security/delivery guarantees, and one logical edge.

The examples are small and share publisher/subscriber binaries. Unicast and multicast terminate
with explicit pass counts. Broadcast uses a preloaded image, disables build/pull during execution,
uses a fixed internal subnet, fails clearly without the image, and cleans up. The native runner is
appropriately isolated by inspection but still requires the runtime evidence in B-001.

## 10. Required action before acceptance

No code remediation is required by this verification pass. To remove the blocker:

1. On a native Linux host, build the current tree and run
   `GRAPHX_VERIFY_LIVE_CAPTURE=1 examples/udp-broadcast/run-native-linux.sh`.
2. Confirm delivery and decoded sequences 1-5.
3. Interrupt one run during setup or execution and confirm the trap cleans up.
4. Run `examples/udp-broadcast/down-native-linux.sh` twice and confirm both calls succeed.
5. Confirm `ip netns list` and `ip link show` contain no GraphX broadcast resources.
6. Attach those exact results to the next independent verification. Rerun at least the focused
   UDP and package gates on that same Linux tree to detect platform-specific regressions.

## 11. Readiness for the SDR topology phase

The code is functionally ready for the planned SDR topology, but the formal Phase 11 gate remains
**BLOCKED**, not accepted. Per the governing verifier contract, do not begin the SDR topology
phase until native Linux broadcast acceptance completes and a follow-up verifier issues
`ACCEPTED`.
