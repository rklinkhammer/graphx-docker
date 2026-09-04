# Phase 11 implementation handoff: UDP edges

Date: 2026-09-04  
Repository: `/Users/rklinkhammer/workspace/graphx-docker`  
Baseline commit at implementation start: `1cbbfd5 install certs`  
Version during implementation: `1.0.0`

## 1. Outcome summary

Phase 11 adds bounded IPv4 UDP edges for unicast, directed or limited
broadcast, and multicast. A datagram contains exactly one existing GraphX
`u32be` frame. The implementation uses the platform socket API, integrates with
the existing configuration loader and `TransportFactory`, and does not change
the envelope wire format or configuration version.

Three deterministic examples are included. Unicast and multicast run locally;
broadcast runs on a fixed, internal Docker subnet so it cannot select or send on
a physical network. The common publisher and subscriber are installed and
included in native packages and the runtime container.

MacOS and Linux-container regression, sanitizer, formatting, static-analysis,
fuzz, packaging, schema, web, telemetry, Wireshark, and example checks pass.
No native Linux host was available in this implementation environment, so
native-host broadcast, namespace, firewall, and physical-interface behavior
remain for independent verification.

The proposed SDR topology was not implemented. Phase 11 supplies only its UDP
transport foundation and small transport examples.

### Post-verification remediation

An earlier independent pass returned **CHANGES REQUIRED**. That update resolved
all five code, test, example, and automation findings from that pass:

| Finding | Remediation | Final evidence | State |
|---|---|---|---|
| F-001 / P1 — descriptor lifetime race | `close()` now wakes blocked receive, retains all descriptors, and waits under the send/receive operation locks before closing them. A 100-iteration test races two close calls with an established infinite receive. | Clean macOS and Linux sanitizer suites pass; the focused UDP group passed 10 consecutive runs. | Remediated; independent native-Linux re-verification required |
| F-002 / P2 — late outbound bound | New public `serialized_size()` performs checked, non-allocating encoded-size calculation. UDP rejects over-limit envelopes before `serialize()`. | Exact-limit, limit-plus-one, and 1 MiB payload regressions pass; the installed external consumer checks the new API. | Remediated |
| F-003 / P2 — unbounded/unverified socket diagnostics | Every socket failure still increments `udp_socket_errors`, while text is limited to one diagnostic per transport per second. Bind failure asserts the distinct counter; a loopback-bound send to TEST-NET-1 deterministically proves three failures produce three counters and one diagnostic on macOS and Linux. | Native macOS CTest and Linux sanitizer gates pass. | Remediated |
| F-004 / P2 — broadcast builds online | Compose now references a preloaded image. The runner explicitly uses `--no-build --pull never`, fails clearly when the image is absent, and documents build/load preparation separately. | A freshly rebuilt current image delivered 5/5 without a build or pull during example execution. | Remediated |
| F-005 / P3 — live capture not automated | Native Linux broadcast acceptance optionally launches bounded `dumpcap` only on its disposable publisher veth, decodes with the checked-in Lua dissector, and requires sequences 1–5. The privileged suite enables it when tools exist and reports a skip otherwise. | Script syntax, ShellCheck, non-Linux refusal, and teardown paths pass locally. The capability-gated capture still requires a native Linux host. | Implemented; native runtime evidence pending |

The remediation does not change the Phase 11 scope. In particular, it does not
start the deferred SDR topology.

### Second post-verification remediation

The 2026-09-04 re-verification found one additional transport defect and one
portable-suite isolation defect. Both are now implemented and covered:

| Finding | Remediation | Final implementation evidence | State |
|---|---|---|---|
| F-001 / P1 — malformed traffic defeats a finite receive deadline | `receive_result()` now preserves a zero-timeout nonblocking probe but checks the original absolute deadline before accepting more work after its first poll and after interrupted waits. Continuous socket readability can no longer extend a finite call. | The verifier's external probe changed from 531 ms to exactly 50 ms while counting 7,150 malformed datagrams. The checked-in eight-sender flood regression passed 20 consecutive macOS runs, Linux C++20/C++23, and ASan/UBSan; the same receiver subsequently accepts a valid envelope using bounded retries that tolerate the kernel's UDP backlog/drop policy. | Remediated; independent re-verification required |
| F-002 / P2 — portable suite leaks `GRAPHX_MAX_MESSAGES=0` | The coordinated-shutdown stage now unsets its message and interval variables. Each UDP example stage also receives an explicit positive count, preventing future ambient-state regressions. | `scripts/test-linux-container.sh portable` completed through C++20/23, every topology, all process examples, telemetry 71/71, web 9/9/build, runtime control, and HTTPS checks. Evidence: `outputs/linux-container/portable-20260904T193136Z.log`. | Remediated |

Post-remediation quality evidence is
`outputs/linux-container/quality-20260904T193254Z.log` and
`outputs/linux-container/sanitizers-20260904T193254Z.log`. LLVM 18 formatting,
clang-tidy, cppcheck, two fuzz targets, and all 24 enabled ASan/UBSan tests plus
sanitizer coverage passed. Native Linux namespace broadcast remains the only
environment-specific acceptance item unavailable on the macOS/OrbStack host.

## 2. Acceptance traceability

| ID | Implementation evidence | Test/runtime evidence | Status |
|---|---|---|---|
| UDP-001 — Configuration | `include/graphx/config.hpp`, `src/config.cpp`, `config/schema/graphx.schema.json`, CLI inspection in `apps/cli/main.cpp` | `tests/test_config.cpp` covers all modes, missing/unknown/wrong-type values, address/mode mismatch, port/TTL/buffer/datagram boundaries, interface syntax, TCP-only keys, framing, and orphan entries; three checked-in configurations pass CLI and schema validation | Implemented; runtime verified on macOS and Linux container |
| UDP-002 — Unicast | `include/graphx/udp_transport.hpp`, `src/udp_transport.cpp`, `src/transport_factory.cpp` | Factory round trip in `tests/test_config.cpp`; content, timeout, framing, and metrics in `tests/test_main.cpp`; `examples/udp-unicast/run.sh` passed repeatedly | Implemented; runtime verified on macOS and Linux container |
| UDP-003 — Broadcast | `SO_BROADCAST` is enabled only for broadcast mode; fixed subnet `172.31.91.0/24` and destination `172.31.91.255`; `run-native-linux.sh` creates two namespaces and an unattached bridge | Current Docker image delivered 5/5 and cleaned up; native script passes syntax, ShellCheck, and non-Linux refusal checks | Implemented; Docker Linux runtime verified. Native Linux-host directed broadcast remains for re-verification |
| UDP-004 — Multicast | Explicit `IP_ADD_MEMBERSHIP`, `IP_DROP_MEMBERSHIP`, interface, TTL, loopback, and reuse options in `src/udp_transport.cpp` | One send reaches two listeners; one listener can leave without disturbing the other; bad interface is rejected; local example produces two `PASS received=5` results | Implemented; runtime verified on macOS and Linux container |
| UDP-005 — Datagram integrity | Bounded `recvmsg`, `MSG_TRUNC`, outer-length validation, drop-and-continue, non-allocating `serialized_size()` preflight, and absolute finite deadlines independent of readability | Empty/short, mismatch, bad magic, unknown version, trailing bytes, truncation, exact maximum, one-byte oversize, 1 MiB preflight, finite burst recovery, and continuous eight-sender malformed-flood deadline/recovery cases | Implemented; runtime and sanitizer verified |
| UDP-006 — Lifecycle | Nonblocking socket plus local cancellation socket; close/operation locks retain descriptors until active I/O exits; move-only public type | Existing lifecycle cases plus 100 iterations of two concurrent close calls against an established infinite receive | Implemented; macOS and Linux-container sanitizer runtime verified |
| UDP-007 — UDP semantics | API exposes no reconnect, retry, TLS, acknowledgement, backpressure, or end-of-stream behavior | Strict configuration rejects TCP-only keys; subscriber rejects an unexpected end-of-stream; loss/duplication/reordering documented and sequence anomalies exercised | Implemented; inspected and runtime verified where controllable |
| UDP-008 — Observability | Default-no-op UDP events, typed counters, fixed sequence window, best-effort callbacks, and one-per-second socket diagnostic limiter | All event categories asserted; bind failure asserts socket counter; cross-platform loopback/TEST-NET send fault asserts three counters and one text diagnostic | Implemented; runtime verified on macOS and Linux container |
| UDP-009 — Capture | UDP port registration plus optional native `dumpcap` capture on disposable `gxudp-ph`, decoded with the checked-in Lua dissector | Generated capture tests pass on macOS/Linux; capability-gated live test is implemented and ShellChecked | Implemented; generated runtime verified. Automated live path awaits native Linux execution |
| UDP-010 — Examples | Three examples; preloaded-image offline broadcast runner; native namespace runner; traps and bounded counts/timeouts; explicit per-stage portable-suite environment | Unicast/multicast passed targeted repetition and the full Linux portable tier; current-image broadcast passed 5/5 using `--no-build --pull never`; native scripts pass static checks | Implemented; runtime verified subject to UDP-003 native-host limitation |
| UDP-011 — Compatibility | Additive transport enum/config/API; unchanged wire/config versions; existing transports untouched except factory integration | Full CTest, golden vectors, mixed-version transport, existing examples, release contract, installed consumer, telemetry, and web tests pass | Implemented; runtime verified |
| UDP-012 — Quality gates | CMake/build/install/container/release manifests and privileged Linux feature suite updated | Final clean 24/24; malformed flood 20 repetitions; complete Linux portable tier; Linux ASan/UBSan, LLVM 18 format/tidy, cppcheck and fuzz; Release/package; telemetry 71/71; web 9/9/build | Implemented; runtime verified. ThreadSanitizer and native Linux host were unavailable/not configured |
| UDP-013 — Documentation | `docs/udp-transport.md`, ADR 0012, README, protocol, test procedure, user guide, Wireshark and example READMEs | Documentation was compared with configuration and socket behavior during implementation | Implemented; inspection verified |

## 3. Architecture and compatibility decisions

### Datagram and role model

- `ConnectionMode::connect` creates a UDP sender. It does not establish a
  session. `ConnectionMode::listen` creates a receiver.
- Every UDP datagram is the existing four-byte big-endian frame size followed
  by exactly one serialized envelope.
- The framed size is bounded before `sendto`. GraphX performs no application
  fragmentation or reassembly.
- Receive storage is allocated once at construction and is never larger than
  `max_datagram_bytes`. Truncated and malformed datagrams are dropped without
  changing a healthy receiver into a terminal state.

### Modes

- Unicast rejects multicast and the limited broadcast destination.
- Broadcast is the only mode that enables `SO_BROADCAST`. Directed-broadcast
  classification depends on the deployed subnet, so configuration cannot infer
  it from the destination alone.
- Multicast configures the outbound interface, TTL, and loopback and explicitly
  joins/leaves the destination group on receivers.
- A multicast edge remains one logical GraphX producer-to-consumer edge. The
  second example listener is diagnostic network fan-out, not graph fan-out.

### Lifecycle and observability

- A nonblocking local socket pair wakes a blocked `poll` immediately on close.
- Cleanup, destruction, and UDP-specific trace callbacks are non-throwing.
- Malformed traffic produces typed counters, not one log line per datagram.
- Recent sequence detection uses a fixed 256-entry array, avoiding receive-path
  allocation and bounding both memory and label/cardinality behavior.
- Gap counts are estimates because a delayed packet can arrive after a gap was
  observed.

### Compatibility

- Configuration remains version 1 and the envelope remains wire versions 1/2.
- `TraceSink::on_udp_event` has a default no-op implementation, preserving
  source compatibility for existing custom sinks.
- TCP, Unix-domain, in-process, and shared-memory implementations and their
  configuration contracts were not changed.
- UDP additions are included in install/export metadata, the release archive
  contract, the runtime image, and external-consumer compilation.

The rationale and consequences are recorded in
`docs/adr/0012-bounded-ipv4-udp-edges.md`.

## 4. Files and public interfaces changed

### New public/runtime implementation

- `include/graphx/udp_transport.hpp`: `UdpOptions`,
  `kMaxUdpDatagramBytes`, and move-only `UdpTransport`.
- `src/udp_transport.cpp`: IPv4 socket implementation.
- `include/graphx/config.hpp`: `TransportKind::udp`, `UdpMode`, and UDP edge
  settings.
- `include/graphx/observability.hpp`: `UdpEvent` and UDP counters.
- `src/transport_factory.cpp`: sender/listener creation from validated edge
  configuration.

### New applications and examples

- `apps/udp_publisher/main.cpp`
- `apps/udp_subscriber/main.cpp`
- `examples/udp-unicast/`
- `examples/udp-broadcast/`
- `examples/udp-multicast/`

### Configuration, capture, tests, packaging, and documentation

- `src/config.cpp`, `config/schema/graphx.schema.json`, `apps/cli/main.cpp`
- `src/observability.cpp`, `apps/common.hpp`
- `wireshark/graphx.lua`, `wireshark/README.md`,
  `scripts/test-wireshark.sh`
- `tests/test_main.cpp`, `tests/test_config.cpp`,
  `apps/telemetry/config-schema.test.mjs`, `tests/test_package.py`
- `CMakeLists.txt`, `Dockerfile`, `scripts/test-features.sh`,
  `scripts/release/release_common.py`
- `README.md`, `CHANGELOG.md`, `docs/protocol.md`,
  `docs/test-procedure.md`, `docs/GraphX_New_Graph_Example_User_Guide.md`,
  `docs/udp-transport.md`, and ADR 0012.

The pre-existing modified `prompt/implement.md` and `prompt/verifier.md` define
the Phase 11 work packages and were preserved as the governing inputs.

## 5. Commands and results

### Clean macOS build and complete regression

```sh
cmake -S . -B build/phase11-clean -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGRAPHX_BUILD_TESTS=ON
cmake --build build/phase11-clean -j 4
ctest --test-dir build/phase11-clean --output-on-failure
```

Result: build passed; 24/24 tests passed in 13.57 seconds in the final complete
run. An initial unicast-example startup race was reproduced, corrected with a
bounded subscriber startup delay, and the targeted UDP tests then passed ten
consecutive repetitions before the final suite.

### Targeted repetition

```sh
ctest --test-dir build/phase11-clean \
  -R 'graphx-udp-(transport|unicast-example|multicast-example)' \
  --repeat until-fail:10 --output-on-failure
```

Result: all three tests passed ten consecutive executions.

### Sanitizers

```sh
cmake -S . -B build/phase11-sanitizers -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=23 \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_ENABLE_SANITIZERS=ON
cmake --build build/phase11-sanitizers -j 4
ctest --test-dir build/phase11-sanitizers --output-on-failure
scripts/test-linux-container.sh sanitizers
```

Result: macOS passed 24 enabled tests and sanitizer-coverage in 15.40 seconds; the package test
was intentionally disabled for the instrumented static archive. Linux container
with Clang 18 and leak detection passed the same 24 enabled tests and coverage.
Evidence: `outputs/linux-container/sanitizers-20260904T175609Z.log`.

### Linux C++20/C++23 regression

```sh
scripts/test-linux-container.sh ctest
```

Result: Ubuntu 24.04 aarch64 Linux container, GCC 13.3.0: 24/24 tests passed for
C++23 and 24/24 for C++20. Evidence:
`outputs/linux-container/ctest-20260904T174715Z.log`.

### Formatting, static analysis, and fuzzing

```sh
GRAPHX_FUZZ_SECONDS=5 scripts/test-linux-container.sh quality
```

Result: `clang-format-18` passed 43 files; `clang-tidy-18` and cppcheck passed
all library, application, and test targets; envelope and frame libFuzzer targets
ran for 5 seconds each without a finding. Evidence:
`outputs/linux-container/quality-20260904T174958Z.log`.

The host invocations of `scripts/check-format.sh` and
`scripts/run-static-analysis.sh` returned 2 because the pinned version-18 tools
are not installed on the macOS host. A supplementary LLVM 23 check passed the
changed UDP production files. A repository-wide LLVM 23 run stops on pre-existing
TCP move diagnostics; that is not substituted for the passing official LLVM 18
container gate.

### Release install and external consumer

```sh
cmake -S . -B build/phase11-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 \
  -DGRAPHX_BUILD_TESTS=ON
cmake --build build/phase11-release -j 4
ctest --test-dir build/phase11-release \
  -R 'graphx-(release-contract|package)' --output-on-failure
```

Result: 2/2 passed. The package test installed GraphX, checked the exact release
archive contract, compiled and ran an external consumer including
`graphx/udp_transport.hpp`, and exercised archive tamper cases.

### Examples and affected container

```sh
examples/udp-unicast/run.sh
examples/udp-multicast/run.sh
docker compose -f examples/udp-broadcast/compose.yaml config --quiet
examples/udp-broadcast/run.sh
```

Result: unicast received 5/5, both multicast listeners received 5/5, and the
isolated broadcast listener received 5/5. The broadcast Compose project exited
zero and its containers and internal network were removed. The runtime image
built successfully on a Linux aarch64 Docker environment. GCC 12 emitted an
existing optimizer `-Wrestrict` warning in `src/observability.cpp`; it did not
involve the UDP additions and did not fail the build.

### Wireshark, telemetry, web, and static file checks

```sh
scripts/test-wireshark.sh /opt/homebrew/bin/tshark \
  build/phase11-clean/graphx-capture-fixture wireshark/graphx.lua
npm test --prefix apps/telemetry
npm test --prefix web
npm run build --prefix web
shellcheck examples/udp-unicast/run.sh examples/udp-multicast/run.sh \
  examples/udp-broadcast/run.sh scripts/test-wireshark.sh scripts/test-features.sh
python3 -m json.tool config/schema/graphx.schema.json
git diff --check
```

Result: Wireshark passed on macOS and Linux; telemetry passed 71/71; web passed
9/9 and built successfully; shell, JSON, Python compilation, and whitespace
checks passed. The web build retained its pre-existing large-chunk warning.

### Final remediation acceptance pass

The source state after all verifier-driven changes was revalidated separately
from the original implementation results above:

```sh
cmake -S . -B build/phase11-remediation-final -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGRAPHX_BUILD_TESTS=ON
cmake --build build/phase11-remediation-final -j 4
ctest --test-dir build/phase11-remediation-final --output-on-failure
ctest --test-dir build/phase11-remediation-final \
  -R 'graphx-udp-(transport|unicast-example|multicast-example)' \
  --repeat until-fail:10 --output-on-failure
```

Result: the clean Debug build passed 24/24 tests; the final full rerun after the
cross-platform socket-fault regression passed 24/24 in 13.13 seconds. The three
focused UDP tests passed ten consecutive executions in 26.92 seconds.

```sh
cmake -S . -B build/phase11-remediation-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 -DGRAPHX_BUILD_TESTS=ON
cmake --build build/phase11-remediation-release -j 4
ctest --test-dir build/phase11-remediation-release \
  -R 'graphx-(release-contract|package)' --output-on-failure
GRAPHX_FUZZ_SECONDS=2 scripts/test-linux-container.sh quality
scripts/test-linux-container.sh sanitizers
```

Result: Release packaging passed 2/2. The final Linux quality gate passed LLVM
18 formatting and static analysis, cppcheck, sanitizer-coverage inspection, and
both fuzz targets. The Linux Clang 18 ASan/UBSan gate passed all 24 enabled
tests plus coverage; the package test was disabled by design for an instrumented
static archive. Evidence is in
`outputs/linux-container/quality-20260904T184342Z.log` and
`outputs/linux-container/sanitizers-20260904T185134Z.log`.

```sh
docker build -t graphx-demo:phase11-remediation .
GRAPHX_BROADCAST_IMAGE=graphx-demo:phase11-remediation \
  examples/udp-broadcast/run.sh
npm test --prefix apps/telemetry
npm test --prefix web
npm run build --prefix web
docker run --rm -v "$PWD:/mnt" koalaman/shellcheck:stable \
  examples/udp-broadcast/run-native-linux.sh \
  examples/udp-broadcast/down-native-linux.sh
bash -n scripts/test-features.sh
git diff --check
```

Result: the current runtime image built, and its broadcast example delivered
5/5 using the documented no-build/no-pull execution. Telemetry passed 71/71;
the web console passed 9/9 and built with its existing chunk-size warning.
Both native broadcast scripts passed ShellCheck, the feature script passed
syntax validation, non-Linux invocation refused safely with exit 2, and the
worktree whitespace check passed.

## 6. Unavailable or deliberately unverified checks

- **Native Linux host networking:** no native Linux host was attached to this
  implementation session. Docker/OrbStack Linux results do not prove native
  host namespace, firewall, directed-broadcast, or physical multicast behavior.
  `examples/udp-broadcast/run-native-linux.sh` is now the exact acceptance
  runner and is integrated into the privileged Linux feature suite.
- **Privileged live packet capture:** the dissector consumed generated standard
  IPv4/UDP PCAP and existing GraphX PCAPNG fixtures. The new optional live path
  is restricted to a disposable veth but was not run because this is not a
  native Linux host with the required namespace/capture privileges.
- **ThreadSanitizer:** the repository has no configured TSan preset. ASan/UBSan
  and lifecycle stress passed; TSan was not claimed.
- **Physical multicast/routing:** only loopback/local-container group behavior
  was tested. Routed multicast, IGMP snooping/querier behavior, and real switch
  configuration remain deployment-specific.

## 7. Known limitations and deferred scope

- IPv6, DTLS, reliability, retransmission, acknowledgement, FEC, congestion
  control, fragmentation/reassembly, and native one-to-many graph edges are out
  of scope.
- UDP cannot reliably report an unavailable receiver; a successful send means
  only that the kernel accepted the datagram.
- Directed-broadcast correctness is subnet-dependent and cannot be inferred
  safely from an IPv4 address without deployment network metadata.
- Kernel socket-buffer values can be clamped or scaled after a successful
  request. Exact read-back equality is intentionally not required.
- Sequence metrics estimate anomalies over a bounded 256-entry recent window;
  they are not proof of permanent packet loss.
- UDP provides neither confidentiality nor authentication. Source addresses can
  be spoofed; TCP/TLS remains the recommended control path.

## 8. Security and denial-of-service considerations

- The receiver allocates one fixed bounded datagram buffer and validates the
  length prefix before deserialization.
- A finite receive uses one absolute deadline. Continuous malformed or
  truncated traffic is counted and dropped but cannot extend that call.
- Oversized outbound messages are rejected before any network operation.
- Malformed traffic increments counters without payload logging or per-packet
  error text, avoiding an unbounded log amplification path.
- Trace-sink failures are swallowed on the UDP data path so unavailable
  telemetry cannot prevent send/receive progress.
- Error context contains edge and operation names, not message payloads.
- The broadcast example is fixed to an internal Docker network and has no host
  network mode, host ports, added capabilities, or writable root filesystem.
- Operators must still apply firewall/rate controls and avoid treating source IP
  or multicast membership as authorization.

## 9. Risks for the independent verifier

1. Run the broadcast example on a true Linux host and prove both network
   isolation and idempotent teardown, including interruption during setup. Use
   `GRAPHX_VERIFY_LIVE_CAPTURE=1` so delivery and live decoding are one gate.
2. Repeat the continuous malformed-datagram flood on native Linux and confirm a
   50 ms finite receive remains bounded, close retains priority, diagnostic
   volume is bounded, and the same receiver later delivers a valid envelope.
3. Repeat the new concurrent double-close/infinite-receive regression on native
   Linux, inspect file-descriptor counts, and run TSan if supported.
4. Independently repeat the cross-platform loopback-to-TEST-NET socket-error
   diagnostic regression and confirm the one-per-second policy meets operational
   needs.
5. Validate multicast on each intended deployment interface and confirm the
   effect of `reuse_address`, loopback, TTL, firewall, and switch IGMP policy.
6. Re-run the documented examples literally from a clean checkout and confirm
   their nonzero failure paths and signal cleanup.

## 10. Scope confirmation

No SDR node, switch topology, signal-processing pipeline, device integration,
or SDR-specific schema was added. Work on that topology should wait for the
independent Phase 11 verifier verdict.
