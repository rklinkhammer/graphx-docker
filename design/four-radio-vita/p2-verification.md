# P2 native processor and detector qualification

Status: **P2 complete for native functional acceptance**. Every P2 exit criterion
below is satisfied. Scope is native, nonprivileged functional
acceptance starting from GraphX `d107fd7`. The VRT dependency remains exactly
`dbe85d37155145842da60367af1c4beef8801b0c`; it is the only VITA codec/runtime.
No P3 work, privileged networking, image publication, push or deployment is included.

## Implementation and exit criteria

| P2 criterion | Application implementation and independent evidence | Assessment |
|---|---|---|
| Design before wire implementation | `processing-design.md` defines numerical normalization, frequency axis, rational bin widths, wire offsets, quality maps and resource bounds | Implemented |
| Four independent receivers and controller | `processing_app.cpp`, four `ControllerSession` instances, source/SID validation, library Context association, bounded five-second configuration, common scheduled start and terminal execution reporting | Passed four real-radio, unavailable-radio, source-mismatch and control-reconnect fixtures |
| Continuous FFT assembly | `processing.cpp`: short packets and bursts do not reset windows; reorder deadline, duplicates, gap padding, metadata rejection, bounded outage skip | Passed focused application tests, including a 1,000-second jump, queue saturation, initial packet loss and RF-change/late-packet regressions |
| Bounded complete power message | Separate `VitaPowerSpectrum` raw UDP schema; all six sizes decoded independently by Python; malformed lengths/metadata/power/padding rejected | Passed; maximum IPv4 packet 8,864 bytes |
| FFT and frequency correctness | Rectangular/Hann, 0/50/75% overlap, signed tones, coherent power .0625 for amplitude .25; deterministic shifted peak estimator | Passed; isolated-tone tolerance half a bin, coherent power tolerance 1e-7 |
| Quality and burst metadata | Valid/missing counts, per-sample gap bitmap, observed boundary bitmap, sample epoch/ordinal and exact endpoints, source sequence discontinuities | Passed C++ loss/reorder/burst tests and independent inspection of live processor output |
| Detector and bounded display | Every received packet creates detection/no-detection/invalid output or a counted display drop; queue 64 lines; sequence-loss and duplicate counters | Passed zero/invalid input and stalled-reader recovery with actual detector process |
| Configuration/catalog/lifecycle | Opt-in type descriptors; authoritative and normalized-node validation; signed gain parameters; existing credentials, telemetry and release barrier | Passed normalization rejection cases and all production process fixtures |
| Real application acceptance and regression | Actual P1 radios → processor → independent UDP relay → actual detector; maximum size/non-divisor rate and absent fourth radio; mTLS reconnect preserves sample time; detector loss does not block sources | Passed; P1 seven-test suite retained unchanged |

The start-epoch reproducer's timestamp assertion is unchanged. Processor time is
anchored to the scheduled UTC start; it does not overwrite received VITA times or
backend/AckX actual execution times. Independent live-output checks verify a common
epoch across streams and `epoch + floor(ordinal * 10^12 / rate)` at both FFT
interval endpoints. Library qualification alone is not used as application evidence.

## Complete message sizes and nominal load

Binary32 bins plus a 128-byte header, N/8 gap bytes and N/8+4 boundary bytes:
`UDP payload = 132 + 17*N/4`. There is no GraphX envelope. IPv4 without options
adds 20 bytes and UDP adds 8. The loader rejects a smaller authored `path_mtu`.

| N | UDP bytes | IPv4 bytes | Bin width at 1 MHz (Hz) | Spectra/s/stream: 0% / 50% / 75% overlap |
|---|---:|---:|---:|---|
| 64 | 404 | 432 | 15625 | 15625 / 31250 / 62500 |
| 128 | 676 | 704 | 7812.5 | 7812.5 / 15625 / 31250 |
| 256 | 1220 | 1248 | 3906.25 | 3906.25 / 7812.5 / 15625 |
| 512 | 2308 | 2336 | 1953.125 | 1953.125 / 3906.25 / 7812.5 |
| 1024 | 4484 | 4512 | 976.5625 | 976.5625 / 1953.125 / 3906.25 |
| 2048 | 8836 | 8864 | 488.28125 | 488.28125 / 976.5625 / 1953.125 |

Multiply rates by four for four radios and by `Fs/1000000` for another sample rate.
The maximum supported parameter combination (2 MHz, N=64, 75% overlap, four
streams) requests 500,000 spectra/s, or 216 MB/s including IPv4/UDP headers.
This is a nominal workload, not a measured zero-loss throughput claim. The
application bounds packet queues (32/stream), deferred header hints (128/stream),
FFT input (N/stream), per-turn receive work and display history. Large outages
skip samples rather than generating unbounded zero spectra. Failed nonblocking
sends, input saturation and display saturation are counted. Packet sequence and
sample gaps expose losses not observable directly at a portable UDP socket.

Live fixtures collect at least 20 independently decoded spectra per active stream,
including FFTs spanning 2,050-pair bursts and short final packets. Counts and elapsed
startup time are printed in the verbose test logs; these are functional evidence,
not sustained-throughput benchmarks. In the final native run, the ordinary fixture
observed 64/66/64/62 spectra in 0.629 s including startup; the reconnect/maximum
fixture observed 288/247/238/251 after its reset observation point within 1.688 s
including startup/reconnect, with 1,280 observed boundaries in the full collection.
The stalled display reported 851 drops and then recovered. The absent-radio case
observed 80/93/83 spectra after its five-second configuration deadline. The unavailable-radio case includes the
five-second admission window. The maximum-size case uses Fs=1,000,003, Hann and
50% overlap. The boundary fixture interrupts an opaque test-owned TCP relay after start and
requires a new authenticated connection with fresh spectra from every stream
without reinitializing sample time. A source-mismatch fixture rejects radio 4
while the other three continue. A separate 5,000-message burst into an unread detector stdout pipe
must report nonzero display drops, recover, and emit zero/invalid results.

## Commands and results

Environment: native macOS arm64, Apple clang 21 for the ordinary build; Homebrew
LLVM 21 and MacOSX26 SDK for UBSan/static analysis. Python fixtures use temporary
mTLS credentials, private catalog locks and owned local processes/sockets only.

```sh
cmake -S . -B build/vita-migration -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_BUILD_VITA_RADIO=ON \
  -DGRAPHX_VRT_REPOSITORY=/Users/rklinkhammer/workspace/vrt_framework
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration -R graphx-vita --output-on-failure -V
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
```

Ordinary VITA suite: **13/13 passed** (71.22 seconds). Repository quick: **41/41 passed**.
Quality: **passed**. Portable: **passed** (124 seconds), including normalized consumers, HTTP/web-console
checks and owned native execution.

Opt-in targets are not covered merely by running the default quality build:

```sh
/opt/homebrew/opt/llvm@21/bin/clang-tidy -p build/vita-migration \
  --extra-arg=-isysroot \
  --extra-arg=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  src/vita/processing.cpp src/vita/processing_app.cpp src/vita/host.cpp \
  tests/test_vita_processing.cpp apps/vita-processor/main.cpp apps/vita-detector/main.cpp
cppcheck --enable=warning,portability --inline-suppr \
  --suppress=missingIncludeSystem --error-exitcode=1 -Iinclude \
  src/vita/processing.cpp src/vita/processing_app.cpp src/vita/host.cpp \
  tests/test_vita_processing.cpp fuzz/vita_power_fuzz.cpp \
  apps/vita-processor/main.cpp apps/vita-detector/main.cpp
```

Both passed. Static analysis identified missing aggregate member initializers in
header-hint storage; these were fixed without disabling diagnostics. Final metadata
review added regressions for an entirely missing first FFT window and an RF
Context change followed by a late packet. Discarding an incomplete window keeps
the original rational sample clock.

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
  -R 'graphx-vita|graphx-sanitizer-coverage' --output-on-failure -V
```

UBSan: **14/14 passed** (84.61 seconds), including an audit of instrumentation in 49 library,
11 application and 19 test translation units. This includes the opt-in processor,
detector, sample assembler and unchanged P1 reproducer, not just default targets.

### Application fuzz coverage

The new `graphx-vita-power-fuzz` target instruments the production power decoder,
encoder and detector, with valid seeds for every supported N. It checks exact
canonical round trips and exercises rejected bytes under UBSan. VRT protocol
coverage remains upstream; this target specifically covers GraphX-owned FFT
metadata and power handling. Existing envelope/frame fuzzers do not cover this
raw protocol, and no VRT requalification is claimed.

```sh
CC=/opt/homebrew/opt/llvm@21/bin/clang \
CXX=/opt/homebrew/opt/llvm@21/bin/clang++ \
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
cmake -S . -B build/vita-power-fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_BUILD_VITA_RADIO=ON \
  -DGRAPHX_VRT_REPOSITORY=/Users/rklinkhammer/workspace/vrt_framework \
  -DGRAPHX_ENABLE_SANITIZERS=ON -DGRAPHX_SANITIZERS=undefined \
  -DGRAPHX_BUILD_FUZZERS=ON
cmake --build build/vita-power-fuzz --target graphx-vita-power-fuzz -j4
mkdir -p build/vita-power-fuzz/corpus
build/vita-migration/graphx-vita-processing-test build/vita-power-fuzz/corpus
UBSAN_OPTIONS=halt_on_error=1 build/vita-power-fuzz/graphx-vita-power-fuzz \
  -max_total_time=15 -max_len=8836 build/vita-power-fuzz/corpus
```

Bounded fuzz: **359,016 inputs in 16 seconds, no failure**; peak reported RSS
40 MB, 1,210 covered edges and 2,318 features. This smoke test is not exhaustive
verification and does not fuzz the assembler state machine or VRT protocol.

## Limitations and phase boundary

- Linux/Lima/OVS, jumbo routing/fragmentation observation, mirror recorder, TCG/KVM,
  containers, image/release qualification and manual browser UI were **not run**. They are
  outside P2 native acceptance; no privileged execution was authorized.
- ASan was **not run** on this macOS host, following the documented P1/LLVM startup
  limitation. UBSan is explicit above; no Linux sanitizer result is claimed.
- No shell scripts changed; ShellCheck is not applicable to this change.
- Burst maps preserve observed boundaries; lost marker packets cannot establish
  unobserved boundaries. Missing samples and sequence discontinuities stay explicit.
- No hardware/GPS-discipline claim, feedback control, automatic tuning or P3 work.
- No upstream library defect blocks the implementation. All remaining platform
  qualification belongs to later phases and must not be described as already passed.

### Final affected-check reruns and retained logs

After the metadata edge-case fixes:

```sh
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration -R 'graphx-vita-(processing|power-wire)' \
  --output-on-failure -V
cmake --build build/vita-ubsan -j4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R 'graphx-vita-(processing|power-wire)|graphx-sanitizer-coverage' \
  --output-on-failure -V
/opt/homebrew/opt/llvm@21/bin/clang-tidy -p build/vita-migration \
  --extra-arg=-isysroot \
  --extra-arg=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  '--header-filter=^/Users/rklinkhammer/workspace/graphx-docker/(include|src|apps|tests|fuzz)/' \
  src/vita/processing.cpp tests/test_vita_processing.cpp
```

Native affected checks: **6/6 passed**. Final affected UBSan: **7/7 passed** (20.36 seconds).
Targeted static analysis passed; the full cppcheck command above was repeated.
No failing check remains from development; an early fixture binding-key typo and
the static-analysis initializer findings were corrected and their gates rerun.

Generated evidence is retained under `outputs/vita-p2-qualification/`: normal
`vita.log`, final `processing.log`, full `ubsan.log`, final `processing-ubsan.log`,
`fuzz.log`, `clang-tidy.log`, `clang-tidy-metadata.log`, `cppcheck.log`, `quick.log`,
`quality.log` and `portable.log`. These are generated local evidence, not maintained
source or committed artifacts. The exact test counts and commands above remain
in this document for review without access to those local files.
