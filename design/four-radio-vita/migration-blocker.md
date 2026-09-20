# VRT runtime migration blocker: scheduled sample epoch

Status: **blocked** on `vrt_framework` commit
`60a290c9b1da2396d3d704ebe52ea6cbcf2fa398`. The selected migration contract is
GraphX commit `7e2616036e34c14e0e0dc3b378311b319353bb3c`.
The maintained standalone application has not been migrated. Do not qualify P1
or ship a replacement based on the library's P17 gates alone.

## Requirement and defect

The [accepted brief](../../docs/vita_system.md#vita-packet-and-timing-requirements)
requires the common scheduled start to be the first-sample timestamp, independent
of container scheduling. The [acknowledgment contract](command-analysis.md) also
requires terminal AckX to report actual device execution time. These are different
quantities when a host activates within its allowed scheduling tolerance.

In the pinned library's `include/vita/runtime/public/runtime.hpp`,
`Stream::effect_binding()` constructs the GraphX sample timeline from
`event.actual_time` after a successful DiscreteIO32 start. It also assigns
`coverage_floor` from that time. A backend honestly returning its actual execution
time therefore moves the simulated sample epoch with host scheduling jitter.
The public source callback writes samples but cannot set packet timestamps.
Returning the scheduled time as actual execution would violate the AckX contract;
rewriting packet timestamps in GraphX would introduce forbidden protocol behavior.

## Minimal reproducer

[tests/repro_vita_start_epoch.cpp](../../tests/repro_vita_start_epoch.cpp) uses
only the public library runtime, a bounded synchronous device backend, injected
UTC time and the library loopback transport. It configures the GraphX profile,
arms a start at `1000 s + 50 ms`, and dispatches either exactly on time or 0.5 ms
late, within the declared 100 ms device tolerance. The backend reports the real
injected execution time; the receiver checks the first sample's timestamp.
No GraphX parser, TLS adapter, Soapy code, privileged operation or physical timing
is involved. The executable returns 0 on success, 1 on the demonstrated epoch
mismatch, and 2 on setup/precondition failure. It is intentionally not registered
in the current application's CTest suite, which still uses the existing codec.

Run from the GraphX repository root, with the library checkout clean at the pin:

```sh
git -C /Users/rklinkhammer/workspace/vrt_framework rev-parse HEAD
mkdir -p outputs/vita-migration-blocker
c++ -std=c++23 -O0 -fno-rtti \
  -I /Users/rklinkhammer/workspace/vrt_framework/include \
  tests/repro_vita_start_epoch.cpp \
  -o outputs/vita-migration-blocker/start-epoch
outputs/vita-migration-blocker/start-epoch --on-time
outputs/vita-migration-blocker/start-epoch
```

Native macOS arm64, Apple clang 21.0.0 (`clang-2100.3.34.2`): compilation succeeded.
The on-time control passes. The delayed case exits 1:

```text
scheduled=1000:50000000000 actual=1000:50500000000 first_sample=1000:50500000000
FAIL: host scheduling jitter changed the simulated sample epoch
```

A trial GraphX Soapy/mTLS integration independently exposed the same issue: its
first radio's sample epoch differed from the requested start by 35,292,907 ps.
The trial changes are not in the maintained application. Local generated evidence
is in `outputs/vita-migration-blocker/`: `trial-migration.patch`, build/configure
logs and the integration failure log. This experiment establishes the timing
blocker, not application migration acceptance; the remaining adapter and negative
checks are incomplete.

## Requirement-to-evidence matrix

| Requirement | Implementation | Verification | Status |
|---|---|---|---|
| Selected library and profile | Pinned P17 `graphx_radio` | Clean local pin; standalone reproducer compiled | Library available; application migration blocked |
| Common scheduled sample epoch | Runtime currently initializes from actual execution | On-time control passes; deterministic 0.5 ms delayed case fails | Blocked upstream |
| Honest device execution time | Reproducer backend reports injected actual time | Successful terminal execution observed with delayed sample timestamp | Must preserve during upstream fix |
| Soapy atomic configuration, capabilities, cancellation and controller APIs | P17 interfaces available; host migration incomplete | Trial integration is not qualification | Pending |
| mTLS framing, deadlines, replay and ownership | Host adapter migration incomplete | Full application regression gates not completed | Pending |
| Continuous IQ, burst/SSI, Context and family counters | P17 implementation; application acceptance incomplete | Epoch defect prevents timing acceptance | Pending |
| Linux/Lima/OVS, release, sanitizers and deployment | No migration release | Not run for this blocked migration | Unrun |

## GraphX gates for the blocker change

- `scripts/verify.sh quick`: passed, 41/41 tests.
- `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable`: passed,
  including normalized consumers, HTTP integration, web tests/build and native
  execution. These gates cover the maintained application, not the trial migration.
- `scripts/verify.sh quality`: formatting passed; static analysis failed in
  unchanged `src/envelope.cpp` because LLVM 21 libc++'s
  `__random/clamp_to_integral.h:47` reports `INFINITY` undeclared. The reproducer
  is compiled separately with the Apple toolchain above.
- No shell scripts changed; ShellCheck is not applicable.
- Privileged Linux/Lima/OVS, Docker, release and sanitizer gates were not run.
  The trial migration's application acceptance failed on the timing assertion;
  no complete migrated application test pass is claimed.

## Upstream change prompt

```text
In vrt_framework, fix the graphx_radio start-timing contract at
60a290c9b1da2396d3d704ebe52ea6cbcf2fa398 before GraphX migration resumes.

Run GraphX tests/repro_vita_start_epoch.cpp using the commands in
GraphX design/four-radio-vita/migration-blocker.md. The on-time control passes;
the delayed case fails: scheduled 1000:50000000000, actual execution
1000:50500000000, first sample 1000:50500000000.

GraphX requires the scheduled UTC start to establish its deterministic simulated
sample epoch, while AckX must carry actual terminal device execution time.
Stream::effect_binding currently initializes SampleTimeline and coverage_floor
from event.actual_time. Separate the scheduled sample epoch from actual execution
and observation timestamps inside the library. Preserve honest AckX effective
time, AckS observation time, Context-before-Data association, host pacing,
cumulative rational sample time, stop/start epochs and idempotent replay. Do not
make the backend lie about execution or require GraphX to rewrite VITA packets.
Provide a public, bounded contract if an additional host binding is necessary.

Add independent tests with different within-tolerance dispatch delays for multiple
radios sharing one scheduled time: first-sample times must match the schedule,
while terminal execution timestamps reflect each actual device activation.
Cover Context timestamps, non-divisor sample rates, subsequent bursts,
cancellation, retries and out-of-tolerance failure. Make both reproducer modes
pass without weakening its timestamp assertion. Re-run applicable library gates,
update P17's requirement-to-evidence matrix and provide a new immutable pin.
Do not change the approved GraphX contract to match the defect.
```
