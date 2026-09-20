# P1 standalone radio verification

Status: **P1 complete for native standalone virtual-radio acceptance**. This does
not qualify Linux/OVS, physical hardware, published images or the P2–P6 system.

The standalone application uses `vrt_framework` commit
`dbe85d37155145842da60367af1c4beef8801b0c` as its sole VITA codec and protocol
runtime. The scheduled sample-epoch blocker is **resolved locally**: both modes
of the unchanged [reproducer](../../tests/repro_vita_start_epoch.cpp) passed before
integration resumed. [Exact reproducer commands/results](migration-blocker.md)
retain the original timestamp assertion.

## Implemented surface

`DeviceBinding`, `HostTransport` and `ControllerSession` are reusable application
adapters under `include/graphx/vita/runtime.hpp` and `src/vita/host.cpp`.
The thin radio executable uses the authoritative normalized bindings, staged
credentials and release barrier. One Soapy device supplies both configuration and
CS16 samples. The library's explicitly selected `graphx_radio` profile owns
encoding, parsing, transactions, scheduling, Context association and packet time.
No GraphX production codec or generated VITA packet classes remain.

The opt-in CMake target links `vita::core`, enforces the clean immutable library
revision and emits dependency locks, upstream license texts and SPDX dependency
inputs. This is P1 native application evidence, not qualification of published
images, the final IQ processor or an OVS graph. See [build instructions](README.md)
and [host binding design](radio-design.md#host-adapter-boundaries).

## Executed commands and results

Environment: native macOS arm64, Apple clang 21, Python 3.14, OpenSSL 3, local
unprivileged sockets and temporary test credentials. The library was fetched from
the supplied local Git repository at the exact pin. No Linux guest was used.

```sh
cmake -S . -B build/vita-migration -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_BUILD_VITA_RADIO=ON \
  -DGRAPHX_VRT_REPOSITORY=/Users/rklinkhammer/workspace/vrt_framework
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration -R graphx-vita --output-on-failure
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
/opt/homebrew/opt/llvm@21/bin/clang-tidy -p build/vita-migration \
  --extra-arg=-isysroot \
  --extra-arg=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  src/vita/host.cpp tests/vita_transport_client.cpp
cppcheck --enable=warning,portability --inline-suppr \
  --suppress=missingIncludeSystem --error-exitcode=1 -Iinclude \
  src/vita/host.cpp tests/vita_transport_client.cpp
```

Build: **passed**. Focused CTest: **7/7 passed**, 52.53 seconds; the four-radio
standalone test took 41.05 seconds including retention expiry and stalled peers.
Quick: **41/41 passed**. Portable: **passed**, including normalized consumers, HTTP,
web and native execution. The default build excludes the opt-in radio, so the
focused gates supply its application evidence.

Quality: **passed**, including repository formatting, clang-tidy and cppcheck.
The launcher probes LLVM 21 against the selected SDK using `<random>` and falls
back to the installed, compiler-verified macOS 26 SDK. macOS 27 `math.h` delegates
INFINITY to a newer Clang `float.h` interface (`__need_infinity_nan`) unavailable
in LLVM 21. No diagnostic is disabled and no application macro is patched.
An explicit incompatible SDKROOT fails the preflight rather than being ignored.

Targeted clang-tidy and cppcheck for `src/vita/host.cpp` and
`tests/vita_transport_client.cpp` passed using the commands above. `shellcheck -x scripts/verify.sh` passed.

### Transport stress and sanitizer qualification

`graphx-vita-transport` drives the production HostTransport callbacks with real
mutually authenticated OpenSSL sessions over 256-byte bounded BIO pairs. It does
not replace SSL operations or the VITA codec. With the peer not reading, it fills
the 3072-byte ordinary budget and 1024-byte cancellation reserve, checks refusal,
and continues sending Context/Data while waiting beyond the two-second output
deadline. It then checks byte-exact partial writes with an 83-byte reader, reconnect recovery,
explicit disconnect and abrupt byte-channel loss during pending writes. Every
accepted submission has exactly one terminal completion; every lease (including
rejected submissions) returns once and completion admission usage returns to zero.
Repeated disconnect/detach cannot complete an operation twice.

The adapter checks control deadlines before datagram selection. Continuous UDP
work therefore cannot starve output expiry. Expiring a partially written TLS
record fails the entire control connection; subsequent records are not written
into that damaged stream. Data/Context delivery remains independent.

The process test also sends duplicate starts without reading replies, disconnects
with output pending and reconnects under the same authenticated identity. Retained
AckV/AckX evidence is replayed without changing execution time or sample epoch.

Explicit LLVM 21 UBSan build and instrumentation audit:

```sh
CC=/opt/homebrew/opt/llvm@21/bin/clang \
CXX=/opt/homebrew/opt/llvm@21/bin/clang++ \
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
cmake -S . -B build/vita-ubsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_BUILD_VITA_RADIO=ON \
  -DGRAPHX_VRT_REPOSITORY=/Users/rklinkhammer/workspace/vrt_framework \
  -DGRAPHX_ENABLE_SANITIZERS=ON -DGRAPHX_SANITIZERS=undefined
cmake --build build/vita-ubsan -j4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R 'graphx-vita|graphx-sanitizer-coverage' --output-on-failure
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R graphx-vita-transport --output-on-failure
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh fuzz
```

UBSan: **8/8 passed**, including the compile-command instrumentation gate, both
unchanged epoch-reproducer modes and four-radio wire test. The final strengthened
transport test also passed separately under UBSan. The unchanged reproducer target
now receives sanitizer flags through CMake. LLVM 21 ASan on this macOS host remains
excluded by the repository's documented runtime-initialization limitation; no
ASan or Linux sanitizer result is claimed.

The standard envelope/frame fuzz targets exercise GraphX's own envelope and
framing, not VITA TLS ingestion or `vrt_framework` protocol decoding. Their results
are supplementary repository evidence. The new adapter has deterministic fault
and stress tests under UBSan, not a new libFuzzer target. Upstream library fuzz
qualification is separate and was not rerun or counted as GraphX acceptance.

One run alongside concurrent builds observed an overdue-sample gap in the
maximum-burst case and failed its strict continuity assertion. Run the four-radio
acceptance without competing builds; its assertions remain unchanged. This remains
a host scheduling limit, not evidence of uninterrupted acquisition under arbitrary
CPU load. The library owns bounded overdue-sample skipping and packet timestamps.

Final repository gates: quick **41/41**, portable **passed**, quality **passed**,
fuzz **passed** (30 seconds per envelope/frame target). No failed final gate remains.
The incompatible-SDK negative preflight deliberately returned 2:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk \
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
```

Generated evidence:

- `outputs/vita-p1-qualification/vita.log` — 7/7 normal opt-in gates.
- `outputs/vita-p1-qualification/ubsan.log` — 8/8, including instrumentation audit.
- `outputs/vita-p1-qualification/transport.log` and `transport-ubsan.log` — final
  transport checks including byte-exact partial writes and credit return.
- `outputs/vita-p1-qualification/clang-tidy.log`, `cppcheck.log`.
- `outputs/vita-p1-qualification/contended-wire.log` — failed host-contention run;
  not credited as a passing gate.
- `outputs/vita-p1-qualification/incompatible-sdk.log` — expected rejection.
- `outputs/verification/20260920T012200Z-quick.log`.
- `outputs/verification/20260920T012415Z-portable.log`.
- `outputs/verification/20260920T012441Z-quality.log`.
- `outputs/verification/20260920T012510Z-fuzz.log`.

## Requirement-to-evidence matrix

| Requirement | Application implementation | Independent application evidence |
|---|---|---|
| Sole pinned codec/runtime | CMake `vita::core`, explicit `graphx_radio`, no generator | Clean-pin configure/build; independent Python bytes |
| Atomic four-setting configuration | Same Soapy device validates whole Settings before one assignment | `graphx-vita-runtime`: incomplete/cross-setting rejection, injected known failure, unknown effects fault and invalidate state; wire invalid-setting diagnostics and unchanged settings |
| Capability limits distinct from settings | Soapy range API into library capabilities; BW<=Fs checked separately | Wire CIF7 Maximum/Minimum and C++ controller before configuration, streaming and stopped; no Precision-as-increment |
| Controller operations/correlation | `ControllerSession::commands()` exposes library APIs | `graphx-vita-controller`: configure, state, admission, cancellation, start, reconnect, status, stop and capabilities over mTLS |
| AckV/X/S and read-only observation | Library transactions with backend actual execution clock | Independent CAM/ID/value checks; ReqX+ReqS order; no early AckX; NO_ACTION status/capability |
| Common scheduled sample epoch | `EffectiveEvent::sample_epoch` resets same source once | Four real processes share one UTC start and activate with distinct induced delays; first Data and preceding Context equal schedule; AckX preserves each actual activation time |
| Continuous phase and rational sample time | Cumulative source ordinal; skipped samples advance phase; library packetization | 1,000,003-pair/s tone, exact rational Data/Context time across bursts; stop/restart new epoch; duplicate/reconnect does not restart |
| Wire layout and bounded bursts | Library packetizer, maximum 1024 pairs and 262144-pair bursts | Independent Data class `00ffffff00000000`, absent Context/Command classes, UTC/picoseconds, big-endian I/Q, 52-byte Context fields, SSI transitions, packet counts; 512 full packets from two maximum bursts plus short/single-packet bursts |
| Timing failure and cancellation | Library scheduling; honest actual backend completion time | Admission then owned process pause beyond tolerance rejects execution; stopped status; controller cancels admitted start |
| Replay and reconnect | Stable authenticated peer/association; persistent runtime; 256 records/2 MiB | Identical/changed command retries, reconnect while streaming, retention exhaustion, expiry and stale ID rejection without mutation |
| Bounded host-owned TLS/framing | StreamIngress/StreamFramer, fixed input/output slots, partial writes, two-second deadlines, deferred completions | Wire malformed/fragmented/coalesced input and unauthorized SAN; handshake/frame stalls; bounded TLS output saturation, partial writes, independent UDP progress, deadline expiry, abrupt loss, exactly-once completion and lease/credit cleanup; pending-reply replay after reconnect |
| Configuration integration | Existing normalizer, private opt-in catalog, raw transport credential bindings | Process harness uses normalized authored fixtures; invalid radio index rejected |
| Dependency/legal inputs | Immutable lock, copied MIT/BSL notices, SPDX input generator | Native configure/build generates inventory and licenses; published release integration remains P5 |

`tests/test_vita_radio.py` is an independent test oracle, not a production protocol
implementation. Library tests alone are not credited as application evidence.
`graphx-vita-runtime` drives the production device binding through library runtime
APIs and adds test-owned physical-effect injection; it does not simulate a physical
Soapy driver or make a hardware atomicity claim.

## P1 exit-criterion assessment

These are the seven standalone verification items in the implementation plan;
platform/release phases are assessed separately.

| P1 criterion | Evidence | Assessment |
|---|---|---|
| 1. Configure/query, no early IQ, scheduled start, multiple bursts, stop/query | Independent four-radio process test and production controller API test | Implemented and tested |
| 2. Independent sample/time oracle; tone, gain, passband, clipping, phase | Python byte/sample oracle plus Soapy device API tests for gain/clipping, passband and supported setting changes | Implemented and tested within the declared virtual-device model |
| 3. Full/short/single/multiple packets, Context, SSI, counts | Two maximum bursts, short final packets, single-packet bursts, exact Context and independent Data/Context counters | Implemented and tested |
| 4. Framing/security/negative commands, duplicates and reconnect | Wire negative cases, replay exhaustion/expiry, pending-reply reconnect and bounded TLS stress | Implemented and tested |
| 5. Scheduled time/tolerance, pacing, shutdown, receiver independence | Common-epoch/delayed-activation tests, missed execution window, continuous UDP during stalled TLS, stop and owned process cleanup | Implemented and tested; no arbitrary-host-load timing guarantee |
| 6. Radio IDs 1–4 and independent instances/settings | Four concurrent processes with distinct rates/bursts and correlated identities | Implemented and tested |
| 7. Selectors, CAM/correlation, ReqX+ReqS order and diagnostics | Independent wire assertions and controller admission/execution/cancellation/state checks | Implemented and tested |

No additional library defect or unresolved protocol requirement was found in this
qualification. The dependency pin and epoch assertions remain unchanged. The
reusable controller is a P1 application adapter; the final IQ processor is P2.

## Remaining qualification and unrun gates

- The final IQ processor, published catalog/images and full graph remain P2–P6.
  The dependency SPDX file is an input inventory, not the complete release SBOM.
- Linux, Lima, OVS/MTU, privileged networking, TCG and KVM gates were **not run**;
  privileged authorization was not given. Native loopback does not establish them.
- Docker/full, release and privileged profiles were not run for this change.
  Explicit opt-in UBSan and the standard fuzz profile are reported above.
  No push, deployment, physical SDR or external infrastructure mutation is part of
  this verification.
- Dynamic credential rotation, explicit authored UDP source-port control and
  hardware clock qualification remain outside this P1 server. Its software UTC
  mapping is simulated, with no GPS/PPS accuracy claim.
- Output saturation and partial-I/O failure paths are tested with bounded TLS BIOs;
  they do not establish all kernel/network fault combinations. Real TCP reconnect
  tests complement them. Timing observations do not guarantee arbitrary-load
  scheduling or physical synchronization.
