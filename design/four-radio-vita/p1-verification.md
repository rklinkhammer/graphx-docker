# P1 standalone radio verification

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
  src/vita/host.cpp src/vita/radio.cpp src/vita/virtual_device.cpp \
  tests/test_vita_runtime.cpp tests/vita_controller_client.cpp
cppcheck --enable=warning,portability --inline-suppr \
  --suppress=missingIncludeSystem --error-exitcode=1 -Iinclude \
  src/vita/host.cpp src/vita/radio.cpp src/vita/virtual_device.cpp \
  tests/test_vita_runtime.cpp tests/vita_controller_client.cpp
```

Build: **passed**. Focused CTest: **6/6 passed**, 50.23 seconds; the four-radio
standalone test took 40.91 seconds including retention expiry and stalled peers.
Quick: **41/41 passed**. Portable: **passed**, including normalized consumers, HTTP,
web and native execution. The default build excludes the opt-in radio, so the
focused gates supply its application evidence.

Quality: formatting passed; the repository static-analysis build failed in
unchanged `src/envelope.cpp` through LLVM 21 libc++
`__random/clamp_to_integral.h:47` (`INFINITY` undeclared). This is an unpassed gate,
not a clean quality claim. Targeted clang-tidy for the five integration sources
above passed (dependency/system warnings suppressed); targeted cppcheck passed.
No shell scripts changed;
ShellCheck is not applicable.

Final focused follow-ups also passed: explicit Context-family counter assertions
in `ctest --test-dir build/vita-migration -R graphx-vita-standalone --output-on-failure`
(1/1, 40.83 seconds), and unknown-effect timestamp handling in
`ctest --test-dir build/vita-migration -R 'graphx-vita-(runtime|controller)$' --output-on-failure`
(2/2, 6.49 seconds after rebuilding).

Generated evidence:

- `outputs/vita-migration/focused.log`
- `outputs/vita-migration/context-counters.log`
- `outputs/vita-migration/device-controller.log`
- `outputs/vita-migration/clang-tidy.log`
- `outputs/vita-migration/cppcheck.log`
- `outputs/verification/20260920T010811Z-quick.log`
- `outputs/verification/20260920T010919Z-portable.log`
- `outputs/verification/20260920T010828Z-quality.log`

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
| Bounded host-owned TLS/framing | StreamIngress/StreamFramer, fixed input/output slots, partial writes, two-second deadlines, deferred completions | Fragmented/coalesced requests, malformed/oversized frames, unauthorized SAN, stalled handshake and partial frame, reconnect/cleanup; retention saturation followed by healthy query |
| Configuration integration | Existing normalizer, private opt-in catalog, raw transport credential bindings | Process harness uses normalized authored fixtures; invalid radio index rejected |
| Dependency/legal inputs | Immutable lock, copied MIT/BSL notices, SPDX input generator | Native configure/build generates inventory and licenses; published release integration remains P5 |

`tests/test_vita_radio.py` is an independent test oracle, not a production protocol
implementation. Library tests alone are not credited as application evidence.
`graphx-vita-runtime` drives the production device binding through library runtime
APIs and adds test-owned physical-effect injection; it does not simulate a physical
Soapy driver or make a hardware atomicity claim.

## Remaining qualification and unrun gates

- The final IQ processor, published catalog/images and full graph remain P2–P6.
  The dependency SPDX file is an input inventory, not the complete release SBOM.
- Linux, Lima, OVS/MTU, privileged networking, TCG and KVM gates were **not run**;
  privileged authorization was not given. Native loopback does not establish them.
- Docker/full, release, sanitizer and fuzz profiles were not run for this change.
  No push, deployment, physical SDR or external infrastructure mutation is part of
  this verification.
- Dynamic credential rotation, explicit authored UDP source-port control and
  hardware clock qualification remain outside this P1 server. Its software UTC
  mapping is simulated, with no GPS/PPS accuracy claim.
- Sustained slow-reader output saturation and exhaustive socket-failure injection
  are not established by the handshake/frame-stall tests. Output admission and
  storage are fixed and deadline bounded; broader transport stress remains useful
  qualification. Timing measurements are host observations, not guarantees under
  arbitrary scheduler load.
