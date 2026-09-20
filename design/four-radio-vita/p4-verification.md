# P4 verification — application availability

## User instructions: where to start

Use the [P3/P4 operator runbook](privileged-verification-runbook.md). It provides:

1. The exact request to finish private test images and the automated live harness.
2. Copy/paste read-only Lima checks from your Mac and instructions for each failure.
3. The artifacts/paths to review before authorizing a run.
4. An explicit authorization prompt, existing CLI recovery commands and case IDs.
5. PASS/FAIL/BLOCKED/NOT RUN criteria and the evidence required to close P4.

**Do not proceed to P5/P6 just to obtain test fixtures.** Preparation is unfinished
P3/P4 qualification work. The full live suite has no existing one-command entry
point yet; the implementor must supply it. You do not need to log into a separate
Linux host. The evidence below is already executed unless explicitly marked pending;
the live case specifications at the end are requirements for the missing harness,
not a list of commands you can run unchanged today.

P4 implementation is present; **P4 is not closed**. Native and portable acceptance
are recorded below. Actual stopped-container/OVS path and recorder fault acceptance
remain pending explicit authorization and a verified private image fixture. P3's
privileged acceptance remains pending independently. No later phase is implemented.

Baseline: GraphX `b3c9228` (P3 implementation), following native P1/P2 acceptance.
The sole VITA codec/runtime remains vrt_framework
`dbe85d37155145842da60367af1c4beef8801b0c`. The start-epoch reproducer is unchanged.
No VITA packet, timestamp, transaction or backend execution behavior is reimplemented.

## Exit criteria and evidence

| Criterion | Implementation / independent evidence | Qualification limit |
|---|---|---|
| Authored bounded policy; transactional defaults | C++ loader, authored/normalized schemas and compiler execution plan select `lifecycle.startup: available` and 100–30000 ms readiness. Default is transactional/30000 ms. Golden fixtures and negative tests cover the normalized addition. | Available policy rejects QEMU/namespace/external graphs; these keep existing transactional behavior. |
| Release available subset | Native apps launch before the shared readiness window; containers launch held until infrastructure verification. Each application, including recorder, gets a readiness attempt. Timeout survivors are stopped before owner-token release. The common ledger retains admission and exact process identities. | Actual container/OVS admission needs live qualification. Failure of a pre-application infrastructure holder is an infrastructure failure, not application admission. |
| Classify safe continuation versus unsafe startup | Temporary exit 75, clean exit, signal death and readiness timeout permit continuation. Invalid/security exit 78 and unknown normal exit reject startup. Platform, release, identity and shared-network failures remain fatal. Native injected failures exercise both branches and rollback. | Docker represents signal death by 128+signal; native waitpid distinguishes signal from normal exit. |
| Missing radio / all radios / processor | Production processor starts configured radios after its bounded five-second configuration window. Tests exercise a real radio's busy listener, certificate-name rejection, missing inputs, all radios absent and processor absent. Status reports unavailable acquisition if processor/all radios are missing. | Readiness is process admission, not measured throughput. |
| Runtime isolation and stale results | Real-radio test kills radio4, observes continuing spectra from radios1–3, kills processor, observes increasing radio packet counters and stale detector results. P2 detector-loss test remains. Per-stream freshness is based on valid produced/received spectra, never heartbeat alone. | Actual recorder death, container no-respawn and OVS-wide outage remain live gates. |
| Bounded retry/work/storage | Existing per-stream queues, bounded UDP work and display queues retained. Failed certificate verification terminates that peer's retries; other connection attempts obey normalized count/backoff. No automatic process/container recreation; Compose restart remains no. | This change does not add a sustained memory benchmark or claim lossless delivery. |
| Ownership and cleanup | Native tests retain each failed application's identity, check no respawn, stop survivors and assert no owned native processes remain. Corrupt executable identity and barrier token reject cleanup. Interruption rolls back. Stopped-container endpoint absence requires exact container ownership and surviving OVS records; live paths retain full MTU/ACL checks. | Linux/Lima inventory and replacement-race evidence pending. |
| Recorder and diagnostic independence | Recorder remains separate and bounded; no archive or restart. If its stopped namespace removes mirror delivery, independent diagnostic capture reports unavailable delivery with retained PID/directory identity checks, never successful reception. Other live paths still require full verification. | Actual namespace disappearance, dumpcap termination, retention and cleanup must be tested together. Current unavailable-delivery behavior does not establish P3 diagnostic-capture continuity; this remains an open acceptance issue. |
| P1/P2 and default graph regression | Unchanged epoch assertions, independent four-radio timing, production FFT/detector and transactional graph tests run with the selected gates below. | Native results do not qualify privileged OVS paths. |

The nominal IQ source payload remains **16 MB/s**. Native tests count spectra and
packet progress; they do not measure OVS wire traffic or best-effort recorder rate.

## Commands and results

Commands run from the repository root on macOS arm64. Logs and optional exact owned
inventories are generated under `outputs/verification/p4-20260920/`.

```sh
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration \
  -R 'graphx-(vita|ownership-state|resource-modules)' --output-on-failure
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
PATH=/opt/homebrew/opt/node@24/bin:$PATH \
  GRAPHX_EXECUTION_EVIDENCE=outputs/verification/p4-20260920/inventories-final \
  python3 tests/test_execution_available.py build/vita-migration
shellcheck -x scripts/test-telemetry-features.sh
python3 tests/test_documentation_consistency.py . build/dev/graphx
```

Native VITA/ownership/resource suite: **21/21 passed**, 106.75 seconds. The additional
certificate-name rejection test passed independently with three healthy streams,
one rejected peer, one connection attempt and no data from the unconfigured radio.
UBSan suite: **22/22 passed**, 121.04 seconds, including opt-in VITA targets and the
sanitizer-coverage check. Final focused reruns after the log-snapshot fix passed: normal ownership/console/resource
checks **3/3**, UBSan ownership/console/resource plus authentication rejection **4/4**.
The full native fault suite and existing transactional lifecycle suite both passed
again on the final implementation.

Quick: **41/41 passed** (110 seconds including build). Portable: **passed** (163
seconds), including all 41 development tests, normalized consumers, HTTP/web and
both transactional and available-policy native lifecycle suites. Quality,
opt-in radio/processor clang-tidy and cppcheck, documentation consistency and
ShellCheck passed. No privileged gates ran.

Existing envelope/frame and P2 spectrum fuzzers do not exercise P4 process/OVS
admission. They were not rerun as P4 coverage; this phase adds deterministic fault,
boundary and concurrent-log tests rather than claiming library fuzz evidence for
the GraphX lifecycle.

Additional exact commands:

```sh
cmake --build build/vita-ubsan -j4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R 'graphx-(vita|ownership-state|resource-modules|sanitizer-coverage)' --output-on-failure
ctest --test-dir build/vita-migration \
  -R 'graphx-(ownership-state|node-console|resource-modules)' --output-on-failure
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R 'graphx-(ownership-state|node-console|resource-modules|vita-availability-auth-failure)' \
  --output-on-failure
python3 tests/test_vita_processing.py build/vita-migration --auth-failure
PATH=/opt/homebrew/opt/node@24/bin:$PATH \
  python3 tests/test_execution.py build/vita-migration .
/opt/homebrew/opt/llvm@21/bin/clang-tidy -p build/vita-migration \
  --extra-arg=-isysroot \
  --extra-arg=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  src/vita/radio.cpp src/vita/processing_app.cpp
cppcheck --enable=warning,portability --inline-suppr --suppress=missingIncludeSystem \
  --error-exitcode=1 -Iinclude src/vita/radio.cpp src/vita/processing_app.cpp
cmake --build build/quality --target graphx-ownership-state-tests -j4
```

The nine retained before/after fault pairs are in `inventories-final/`: one temporary
exit, signal death and timeout for each of three common-lifecycle test applications.
Each before-stop ledger retains five native identities (platform, three applications
including the failed one, and console relay). Each after-stop ledger has zero native
process records; barriers are absent and history/logs remain. These are native owned
inventories, not container or OVS inventories. Actual production radio/processor/
detector fault outcomes are separately asserted by `test_vita_processing.py`.

There are **no remaining failed nonprivileged checks**. Remaining acceptance work is
the verified image fixture and authorized privileged plan below, including actual
recorder/application-container failure and shared-OVS outage. P4 must not be marked
complete from the native evidence alone.

Initial quick checks rejected the expected normalized/compiler golden differences;
fixtures were regenerated from the authoritative C++ compiler after reviewing the
new default lifecycle fields. Initial quality rejected an escaping exception in
the fault-injection test executable; the executable now catches and reports it.
A repeated native fault run exposed the immutable-document reader rejecting an
actively appended readiness log (`E_BOUND: input changed while reading`). Readiness
now uses a bounded regular-file snapshot; configuration reading stays immutable.
A concurrent-append test and oversized/symlink/hardlink rejection tests cover the
fix. No diagnostic or acceptance assertion was disabled.

## P4 live case specifications

**Not executed.** Follow the numbered operator steps and P4 case matrix in the
[shared runbook](privileged-verification-runbook.md) before using these specifications.

Use the P3 isolated test fixture and image-verification prerequisites in
[p3-verification.md](p3-verification.md). Select the available policy with a 5000 ms
budget through the authored graph and normal compiler. Work only in the authorized
Linux/Lima guest under `/var/lib/graphx`, with a unique graph identity, no physical
uplinks and no external management/socket exposure. Preserve before/after container
IDs, namespace inodes, veth ifindices, OVS UUIDs, capture PID/directory identities
and an unrelated control graph's inventory.

1. Establish normal readiness and observed spectra/recorder progress. Record all
   container RestartCount values and the release token. This requires real P1/P2
   applications and the passive recorder, not a test-only container replacement.
2. For each radio, processor, detector and recorder, terminate the application
   after network-ready but before application readiness. Also stall each until the
   deadline. Verify the available subset releases, no late join or restart occurs,
   and each unavailable function is reported honestly. A missing processor must
   produce no initial radio acquisition. All radios unavailable must produce no
   useful spectra.
3. Repeat failures after release. Verify healthy-radio packet and spectrum progress;
   after processor death, radio counters continue while detector results become
   stale. Detector/recorder death must leave upstream processes running.
4. Exercise recorder death with diagnostic capture enabled and disabled. Verify
   stopped namespace/veth absence is distinguished from replacement, capture
   evidence is retained and independent diagnostics continue as required by P3.
   Merely reporting unavailable delivery or retaining old packets does not pass
   continuity. Record a failure/blocker if the recorder namespace removes delivery;
   fix the implementation against the approved contract before closing this case. Preserve passive enforcement and image permissions.
5. Remove the owned shared bridge in the isolated fixture and confirm unavailable
   paths, bounded surviving processes and no throughput claim. Startup on a bad
   MTU, wrong ACL, replaced interface/container, wrong token or invalid credentials
   must reject release; runtime status must not repair or mutate resources.
6. Interrupt startup and shutdown at recorded boundaries; verify owned cleanup,
   absent barriers, retained logs/history/capture and untouched unrelated inventory.
   Explicit whole-graph restart must be the only restoration and must document
   interruption of healthy nodes. No individual-container hot replacement is claimed.

These steps require explicit authorization. No privileged Linux, Lima, TCG or KVM
run, image publication, push or deployment was performed for P4.
