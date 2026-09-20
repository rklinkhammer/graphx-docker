# Four-radio VITA requirement verification

P6 passed in the explicitly authorized GraphX Lima guest on 2026-09-20. All five
cases use the same freshly rebuilt normal image release, qualification hooks OFF,
and new containers for each case. Every failure case also verifies explicit
whole-graph recovery with another new container set. Native checks, image packaging,
Lima execution and browser observation are separate evidence categories below.

The [acceptance design](acceptance-design.md) defines measurement bounds. The
[generated acceptance results](../../outputs/verification/p6/acceptance-results.json)
contain raw measurements, case results, browser observations and the final inventory.
The requirement matrix combines focused phase evidence with integrated P6 evidence;
P6 does not replace protocol-boundary or malformed-input coverage from earlier phases.

| Requirement | Implementation | Verification | Status |
| --- | --- | --- | --- |
| Configuration | [vita.radio.json](../../config/catalog/types/vita.radio.json) | [Phase evidence](p5-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Protocol | [runtime.hpp](../../include/graphx/vita/runtime.hpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Timing | [host.cpp](../../src/vita/host.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Burst/count handling | [radio.cpp](../../src/vita/radio.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Jumbo frames | [endpoint_resources.cpp](../../src/infra/endpoint_resources.cpp) | [Phase evidence](p3-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Signal | [virtual_device.cpp](../../src/vita/virtual_device.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| OVS delivery | [graphx.yml](../../examples/four-radio-vita/graphx.yml) | [Phase evidence](p3-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Recorder placeholder | [recorder.cpp](../../src/vita/recorder.cpp) | [Phase evidence](p3-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Diagnostic capture | [capture_resources.cpp](../../src/infra/capture_resources.cpp) | [Phase evidence](p3-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Initial configuration | [processing_app.cpp](../../src/vita/processing_app.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Scheduled start | [processing_app.cpp](../../src/vita/processing_app.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| VITA control codec | [runtime.hpp](../../include/graphx/vita/runtime.hpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Radio service | [radio.cpp](../../src/vita/radio.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Radio control | [processing_app.cpp](../../src/vita/processing_app.cpp) | [Phase evidence](p1-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Channel independence | [processing_app.cpp](../../src/vita/processing_app.cpp) | [Phase evidence](p2-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Processing | [processing.cpp](../../src/vita/processing.cpp) | [Phase evidence](p2-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Resource bounds | [processing.cpp](../../src/vita/processing.cpp) | [Phase evidence](p4-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Observation/security | [http-routes.mjs](../../apps/telemetry/http-routes.mjs) | [Phase evidence](p5-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Lifecycle | [compose_execution.cpp](../../src/infra/compose_execution.cpp) | [Phase evidence](p4-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Feature detection | [processing.cpp](../../src/vita/processing.cpp) | [Phase evidence](p2-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |
| Degraded operation | [application_lifecycle.hpp](../../src/infra/application_lifecycle.hpp) | [Phase evidence](p4-verification.md); [P6 acceptance](../../outputs/verification/p6/acceptance-results.json) | Passed |

## Sustained and fault acceptance

The final baseline sampled all seven applications for 180 seconds. All four streams
shared the scheduled epoch and the context immediately preceding their first IQ
packet. The independent observer decoded 1,071,274 frames during baseline, with zero
IP fragments and maximum UDP payloads of 4,128 bytes (IQ) and 8,836 bytes (spectrum).
Both the full-run mirror observer and impaired processor-ingress observer reported
zero kernel drops and no decoder error.

| Stream | Measured samples/s | First arrival after epoch (ms) | Maximum tone error (Hz) |
| --- | ---: | ---: | ---: |
| 1 | 999,350.712 | 0.590 | 195.31250 |
| 2 | 999,356.358 | 0.638 | 97.65625 |
| 3 | 999,367.650 | 0.537 | 97.65625 |
| 4 | 999,424.109 | 0.558 | 195.31250 |

Requested rate was 1,000,000 samples/s per stream; measured deviations were below
0.065%, within the declared 2% laboratory bound. First-arrival skew was 0.100 ms,
and every first arrival was within the existing 100 ms tolerance. Frequency error
was below half of the 488.28125 Hz bin width. Numerical IQ/phase checks, rational
sample timestamps, full-burst markers and spectrum quality maps passed.

Peak application RSS was 26.1 MiB (processor), below each 128 MiB limit. No
application's second-half RSS peak exceeded its first-half peak. All seven
application counters progressed, identities stayed stable and restart counts were zero.

The explicit 20-second `iq-loss-jitter` scenario applied/cleared the owned processor
ingress fault. Its observation interval recorded 25,883 reordered IQ packets and
1,368 padded spectra. All four authenticated control sessions remained connected,
all application counters progressed, and clearing the fault and shutdown passed.
The scenario uses the ordinary CLI and common ownership validation.

| Failure case | Observed result | Recovery / cleanup |
| --- | --- | --- |
| radio1 | Radio unavailable; stream 1 stale; streams 2–4 available and connected | Passed; new containers; sentinel preserved |
| detector | Detector unavailable; upstream applications continued | Passed; new containers; sentinel preserved |
| processor | Processor unavailable; detector stale; radios/recorder continued | Passed; new containers; sentinel preserved |
| recorder | Recorder unavailable; forwarding and independent diagnostic capture continued | Passed; new containers; sentinel preserved |

Each victim remained stopped without automatic restart. Status reported unavailable
honestly. Each explicit down/up restored counter progress in all seven applications
with new container IDs. Final shutdowns took 3.27–3.55 seconds, below the 60-second
bound; the intermediate shutdowns also satisfied that bound.

Diagnostic capture retained at most two files within the configured 4 MiB rotation
bound plus the permitted final-frame/block overhead. Fresh capture timestamps after
recorder death prove independent capture continued. Capture bytes and protocol
comparisons are additionally covered by the linked P3 evidence.

## Browser observation

Safari on macOS authenticated through the one-use handoff at `127.0.0.1:18080`.
Actual inspection covered seven running applications, nine logical edges, the
`10.79.0.0/24` OVS/switch/SPAN paths, detector logs showing all four RF tones with
validity fields, and the ready diagnostic Ethernet capture catalog. The degraded
case showed radio1 OFFLINE and one node not ready; processor logs showed stream 1
stale/control unavailable while streams 2–4 remained available/connected.

The session was observation-only and control buttons were disabled. Unobserved raw
edge metrics display unavailable values rather than fabricated zeros or a claimed
disconnection. Rates and radio-control state are established by packet evidence and
application logs, not unpopulated generic edge telemetry. The capture catalog's
64 MiB general file limit is separate from this example's smaller two-file/4 MiB
infrastructure rotation settings. Browser checkpoint notes are retained in the
generated results; checkpoint files alone are not treated as browser proof.

## Candidate and verification environment

- Identity-matched Lima fingerprint:
  `995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e`.
- Linux ARM64 in Lima, four CPUs and 8 GiB memory; local Docker and system OVS.
- Images: `/var/lib/graphx/verification/p6/images-recovery`.
- CLI: `/var/lib/graphx/verification/p6/cli/graphx`.
- Guest evidence: `/var/lib/graphx/verification/p6/run6`.
- Workspace: `/var/lib/graphx/verification/p6/examples`; instances `vita-p6-run6-CASE`.
- Subnet `10.79.0.0/24`; console guest loopback `8080`, Mac loopback `18080`.
- Images were built without cache on OrbStack and passed reproducibility, OCI,
  digest, SPDX and executable smoke checks. Exact packaged radio bytes passed the
  strict P1 four-radio harness in a fresh unprivileged Linux container in Lima.

| Normal image | Manifest digest |
| --- | --- |
| runtime | `sha256:520e7f74890925a62fbb1d1ef6ee70ecf63a5412b167c15551e141deff875ff5` |
| sdr | `sha256:969bef37bd9e04e712e349c30a613406467835065c2d93d98b5f78136a18e7f1` |
| telemetry | `sha256:88c875f643a09b2c613075bea74af91e21f3c495fe1fd62dfaea1d3e2e3f534e` |
| vita | `sha256:cd416c29a0c807108771c2a19e9ece32e0a91d9892efb2e3b2b55f4e10b233a1` |

Derived catalog SHA-256: `8b54e8b16693c255af39b5f4b256462d407b56b6e1e5a39dc79a8114eff0d6e0`.

## Verification and current limits

- macOS `quick`: 43/43 passed (`recovery-quick.log`).
- macOS `quality`: formatting, clang-tidy and cppcheck passed (`recovery-quality.log`).
- macOS `portable`: normalized consumers, HTTP integration, web tests/build and
  native execution/failure matrix passed in 176 seconds (`recovery-portable-final.log`).
- Native ownership regressions passed on macOS and Linux in Lima: exact exit status
  across rapid child exits, substituted identities rejected, shared container-service
  bindings, and inactive-versus-incomplete cleanup classification.
- The HTTP security test waits for actual service readiness before asserting it;
  its focused regression and full portable suite passed (`readiness-regression.log`).
- Wire/quality/path/context rejection tests, source examples, deterministic compiler
  goldens, documentation consistency and `git diff --check` passed.
- No shell scripts changed; ShellCheck was not applicable. No native bare-metal
  Linux laboratory, physical radio, QEMU guest, TCG or KVM execution is claimed.

Host evidence is rooted at `outputs/verification/p6/`: `images-recovery-build.log`,
`stage-recovery.log`, `radio1-run6.log`, `failures-run6.log`, `baseline-run6.log`,
`acceptance-results.json` and `application-counters.json`.

This is bounded best-effort host streaming, not a lossless real-time guarantee.
Baseline included 419 padded spectra from missing source samples. End-of-run radio
logs report 119,808–140,288 skipped samples per source, zero UDP errors and zero
clipping; these counters include the later fault interval. Intentional netem loss,
source skips, receiver quality gaps and observer drops remain distinct. Recorder
counters reported zero kernel drops, truncation or invalid frames and `persistence=none`.
No final-packet drain, automatic retuning/restart, individual-container recovery,
persistent recording, GPS discipline or physical-device behavior is promised.

Final read-only inventory confirmed zero containers, OVS bridges, network namespaces,
GraphX-owned links, netem qdiscs, GraphX/dumpcap processes or console listeners. Only
default Docker networks remained. Every unrelated sentinel survived graph operations
and was then removed by exact identity. Preexisting volumes were preserved; owned
history and capture evidence remain in the guest. The browser verification tab was closed.
