# GraphX Phase 2 implementation handoff

Date: 2026-08-31  
Work package: Runtime lifecycle, bounded queues, cancellation, reconnect, and graceful shutdown

## 1. Outcome summary

Phase 2 is implemented and ready for independent re-verification. All built-in
transports now expose deterministic typed receive outcomes. The previously
unbounded in-process queue is bounded and configurable. Unix-domain listener
startup is interruptible before the first peer, and its connect/send operations
have deadlines. Shared-memory close no longer unmaps storage while a blocked
receive may still be returning. Runtime applications distinguish idle timeout,
peer completion, and local cancellation. Existing TCP retry/reconnect and
bounded-send behavior remains intact and is covered by regression tests.

The first independent verification returned **CHANGES REQUIRED**. Its P1 Unix
stream-integrity defect and P2 exceptional-close/descriptor-ownership defects
are remediated. Unix framing and framed-write failures now make the single-peer
transport terminal, trace observer failures cannot prevent close/cancellation,
and every newly accepted Unix descriptor is scoped before configuration. The
retained independent adversarial program now passes every case.

No wire format changed. Configuration version remains 1 because all new keys are
optional and have documented defaults. The version-1 `receive()` virtual method
and three-argument Unix-domain factory functions remain available. No commit,
push, deployment, or publication was performed.

## 2. Requirements implemented

Concrete requirements and acceptance criteria established before editing:

| Requirement | Acceptance criterion | Result |
|---|---|---|
| Distinct receive outcomes | Message, timeout, peer end-of-stream, local cancellation, and transport failure are not conflated | Complete: four typed statuses; failures remain exceptions |
| Bounded local queue | No unbounded in-process allocation; capacity is 1..65,536 | Complete: default 64, schema/loader/factory enforcement |
| Explicit backpressure | Block with deadline or reject immediately | Complete: `block`/`reject`, `send_timeout_ms`, trace callbacks |
| Cancellable waits | Close wakes blocked accept/receive/queue waits | Complete for built-in transports; tests synchronize blocked receives before close, including throwing-observer paths |
| Terminal stream failures | A partially consumed/written or invalid Unix frame cannot leave a usable desynchronized stream | Complete: accepted socket and retained listener close before failure returns; later receive is EOS |
| Exceptional descriptor ownership | Accepted Unix descriptors close if setup throws | Complete: immediate scoped ownership, released only after configuration succeeds |
| Bounded socket operations | TCP and Unix-domain connect/send work cannot block indefinitely | Complete: existing TCP deadlines retained; Unix deadlines added |
| Reconnect behavior | Existing TCP listener reaccept and client retry behavior remains passing | Complete: regression suite passes; delivery remains at-least-once |
| Graceful runtime shutdown | Demo nodes react to signal-safe stop state and distinguish receive outcomes | Complete: transform/sink use typed results; finite and SIGTERM pipelines pass |
| Compatibility | No wire change; existing receive callers and transport subclasses still compile | Complete: legacy virtual adapter test and old Unix overloads |
| Documentation | Limits, ownership, compatibility, and decisions match code | Complete: README, transport docs, lifecycle doc, ADR 0003 |

Important invariants:

- A `message` result always owns an envelope; other built-in results do not.
- Timeout means no frame bytes were consumed. Timeout or closure during a partial
  stream frame is an exception because framing synchronization is lost. A Unix
  framing/protocol/write failure invalidates that version-1 transport before it
  returns; subsequent receive reports end-of-stream.
- Local `close()` is idempotent and reports cancellation to an active receive.
  Peer orderly close reports end-of-stream after committed local/shared-memory
  messages drain.
- A caller may close a transport from a control thread, but must join the active
  operation before destroying the transport object.
- In-process endpoints sharing a channel name must use identical queue settings.
- No application overflow queue is allocated for TCP, Unix-domain, shared memory,
  or in-process backpressure.
- Resource cancellation is authoritative; trace observer callbacks are
  best-effort and cannot make `close()` fail.

Failure cases and limits covered include invalid capacities/policies/deadlines,
queue rejection, blocked-send deadline/cancellation, peer closure, partial-frame
timeout/closure, malformed/oversized frames, reconnect, shared-memory peer death,
and listener cancellation before first connection.

## 3. Architecture and compatibility decisions

`ReceiveStatus` and `ReceiveResult` were added to the transport-neutral core.
Built-in transports override `receive_result()`. The original virtual
`receive()` slot remains the compatibility API and returns an optional envelope.
An existing third-party transport that only overrides `receive()` inherits a
typed adapter; because its old API supplied no lifecycle information, an empty
optional is conservatively classified as timeout. ADR 0003 records this seam.

The in-process channel uses one bounded FIFO with `not_empty` and `not_full`
conditions. `block` waits only to the configured deadline; `reject` allocates no
overflow storage. Close wakes senders and receivers. Factory-level consistency
checking prevents two endpoint configurations from silently disagreeing about a
named channel's limits.

Unix-domain `listen()` now binds/listens and returns. The first typed receive
performs poll/accept under the receive deadline. Accepted sockets are nonblocking
internally so a single deadline covers each frame and each complete send. Version
1 intentionally remains single-peer and does not silently add reconnect. A
private socket pair supplies an explicit cancellation event; this avoids relying
on cross-thread descriptor close to wake `poll`, which is not portable on macOS.
Any failure that may have consumed or emitted part of a frame closes both the
peer socket and retained listener. Accepted descriptors are held by a scoped
owner during setup, preventing a `setsockopt`/`fcntl` exception from leaking the
descriptor.

Shared-memory close separates cancellation from final resource release. Close
marks the shared channel closed, broadcasts waiters, and unlinks the owner's
name; unmapping/file-descriptor release occurs at destruction after the caller
has joined active operations. Existing bounded SPSC and peer-death semantics are
preserved. Close performs the state transition and waiter broadcast before its
best-effort observer callback; in-process close follows the same ordering.

During remediation, a synchronized regression exposed that TCP listener close
could race with an already-entered infinite `poll` on macOS. TCP accept now uses
a 25 ms cancellation slice, so close is observed without relying on descriptor
close to wake the host poll implementation.

## 4. Files and major components changed

- Core API and transport implementations:
  `include/graphx/transport.hpp`, all four built-in transport headers,
  `src/in_process_transport.cpp`, `src/tcp_transport.cpp`,
  `src/unix_domain_socket_transport.cpp`, `src/shared_memory_transport.cpp`.
- Configuration/factory:
  `src/config.cpp`, `src/transport_factory.cpp`,
  `config/schema/graphx.schema.json`, `config/transport-topology.yaml`.
- Runtime applications: `apps/transform/main.cpp`, `apps/sink/main.cpp`.
- Tests: `tests/test_main.cpp`, `tests/test_config.cpp`.
- Documentation: `README.md`, `docs/runtime-lifecycle.md`, TCP/shared-memory
  updates, new in-process and Unix-domain transport guides, and
  `docs/adr/0003-typed-receive-and-bounded-runtime.md`.

The assignment edit in `prompt/implement.md` was preserved. The stale handoff
reference in `prompt/verifier.md` was corrected from `phase_1_handoff.md` to
`phase_2_handoff.md` as required by the verification report.

## 5. Tests and checks run

| Check | Command | Exact result |
|---|---|---|
| Clean debug configure/build | fresh configure/build in `/tmp/graphx-phase2-clean` | Passed; final CTest was 8/8 in 2.35 seconds |
| Native CTest | `ctest --test-dir build-phase2 --output-on-failure` | 8/8 CTest targets passed |
| Detailed native tests | `build-phase2/graphx-tests` and `build-phase2/graphx-config-tests` | 28 runtime/transport cases and 24 configuration/factory cases passed |
| Independent adversarial probe | Compile retained `work/phase2_adversarial.cpp` against the rebuilt library, then run it | Exit 0; Unix partial-frame follow-up was `end_of_stream`; all 6 named cases passed |
| Repetition | Run `graphx-tests` and `graphx-config-tests` 10 consecutive times | 10/10 repetitions passed |
| Full portable suite | `GRAPHX_BUILD_JOBS=4 ./scripts/test-features.sh portable` | Exit 0; C++23 8/8, C++20 8/8, all topology validations, TCP and shared-memory pipelines, SIGTERM shutdown, web build, telemetry/API/control tests passed |
| ASan + UBSan | fresh sanitizer configure/build, then `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ctest --test-dir /tmp/graphx-phase2-asan --output-on-failure` | 8/8 passed in 4.42 seconds; no ASan/UBSan findings |
| ThreadSanitizer | fresh TSan configure/build, then `ctest --test-dir /tmp/graphx-phase2-tsan --output-on-failure` | 8/8 passed in 14.03 seconds; no race findings |
| Compose validation/build | `docker compose -f compose.yaml config` and `docker compose -f compose.yaml build` | Passed; GNU 12 Linux release build passed |
| Live Compose smoke | `docker compose up -d`, `scripts/demo.sh verify`, `docker compose down --remove-orphans` | Five PASS assertions: four services, two connected edges, both edges advancing, API/metrics live; clean teardown |
| Formatting | `git clang-format --diff HEAD -- <changed C++ files>` | No files would be modified |
| Patch hygiene | `git diff --check` | Passed |
| Static analysis | `cppcheck --enable=warning,performance,portability ...` | Exit 0; performance-only pass-by-value advisories, no warning/portability defect |
| Dependency audit | `npm audit --prefix apps/telemetry --omit=dev`; `npm audit --prefix web` | 0 vulnerabilities in both |

Additional test coverage added:

- source compatibility for a legacy transport subclass;
- typed in-process timeout/message/end-of-stream/cancellation;
- bounded in-process reject, block, blocked-send cancellation, and drain;
- invalid and inconsistent in-process queue configuration;
- TCP listener close cancellation regression;
- Unix listener timeout and cancellation before first accept;
- Unix blocked-send deadline and invalid deadline rejection;
- Unix deadline schema/loader validation;
- Unix partial-header timeout, partial-payload closure, oversized prefix, and
  malformed-envelope invalidation with post-error EOS assertions;
- post-send-timeout receive/send terminal-state assertions;
- throwing-observer close while every built-in transport has a blocked receive;
- enabled/disabled TCP reconnect behavior after clean peer closure.

An initial TSan run exposed that cross-thread close alone did not reliably wake a
Unix listener poll on macOS. The explicit cancellation socket fixed that defect;
the rebuilt TSan suite then passed 8/8. LeakSanitizer was disabled because this
Apple sanitizer environment does not provide reliable leak checking; a native
Linux leak-checking job remains an explicit Phase 4 CI follow-up.

## 6. Known limitations

- The legacy typed adapter cannot infer end-of-stream or cancellation from an
  old transport's empty optional. Built-in transports provide full status.
- Concurrent close is supported as a wakeup mechanism; concurrent destruction is
  not. Callers retain responsibility for joining active operations.
- Unix-domain version 1 accepts one peer and has no reconnect policy.
- Accepted-socket setup failure is protected structurally by immediate RAII
  ownership. A deterministic injected `setsockopt`/`fcntl` failure was not run
  because the implementation deliberately exposes no production fault-injection
  switch; this path is verified by ownership inspection rather than runtime.
- TCP reconnect provides at-least-once, not exactly-once, delivery.
- In-process close closes the shared named channel for all endpoints; it is not a
  multi-producer/multi-consumer session broker.
- Shared memory remains single-producer/single-consumer and PID liveness is
  best-effort. Robust abandoned-mutex recovery remains Linux-only.
- Native privileged Linux macvlan/ipvlan/OVS/netns labs were not run on this macOS
  host. They are outside the Phase 2 transport-lifecycle change; the Linux
  container release build and TCP Compose runtime were exercised.

## 7. Risks for independent verification

1. Repeat close-versus-receive and close-versus-send stress under native Linux
   TSan in addition to the passing Apple TSan suite.
2. Confirm the added transport virtual is acceptable under the project's future
   binary-compatibility policy. Source compatibility is tested; the project does
   not yet publish a stable shared-library ABI.
3. Add a test-build-only socket-configuration injection seam if Phase 4 adopts a
   general syscall fault-injection framework; do not add a production environment
   switch solely for this case.
4. Verify shared-memory cancel/unlink ordering with repeated cross-process start,
   close, crash, and recreation cycles on Linux.
5. Confirm downstream custom transports adopt typed outcomes rather than relying
   indefinitely on the legacy empty-optional adapter.

## 8. Acceptance-criteria checklist

- [x] Runtime distinguishes timeout, cancellation, end-of-stream, and failure.
- [x] All application-level queues introduced by built-in transports are bounded.
- [x] Queue capacity, policy, and deadlines are explicit and validated.
- [x] Blocked built-in receives/accepts can be cancelled with idempotent close.
- [x] Socket and shared-memory resources follow explicit RAII ownership.
- [x] Unix frame/write failures transition to a safe terminal state.
- [x] Close/cancellation does not depend on trace observer success.
- [x] Existing TCP retry, reconnect, partial-frame, SIGPIPE, and backpressure tests pass.
- [x] Demo runtimes shut down cleanly under SIGTERM and finite end-of-stream runs.
- [x] No wire-format change; additive configuration defaults are documented.
- [x] Legacy receive callers/subclasses have a documented compatibility path.
- [x] Unit, negative, integration, sanitizer, portable, and container checks pass as reported.
- [x] README, technical docs, and ADR match implemented behavior.
- [x] No required Phase 2 implementation work is hidden in follow-up language.

## 9. Recommended next work package

After independent Phase 2 acceptance, proceed to Phase 3 only: protocol
specification, compatibility rules, and message/trace identities. Do not fold
Phase 4 CI/sanitizer infrastructure into that implementation, but record the
macOS TSan limitation so Phase 4 can add a native Linux TSan job and split
fork-heavy tests where necessary.
