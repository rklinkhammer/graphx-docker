# Application availability and release

The authored graph may select `lifecycle: {startup: available, readiness_ms: 5000}`.
The default is `startup: transactional`, with a 30000 ms readiness budget. The
available policy applies to native and container applications; namespace, QEMU
and external nodes reject this policy. It does not change platform readiness,
credential staging, release verification or infrastructure ownership requirements.
The normalized graph carries the selected policy. Compose retains `restart: no`.

Start the platform first, then attempt every application, including the recorder.
Application readiness is local binding, not successful peer configuration. Use a
shared bounded application readiness window. A ready application waits at the
owner-token release barrier. Available-policy node barriers have a 600000 ms ceiling
for an orphaned startup; the runner's selected application-readiness deadline is
separate and remains at most 30000 ms. Before publishing that barrier, stop any live
application that missed the deadline, so it cannot join later accidentally.
Keep its exact identity in the common ownership ledger for status and cleanup.
An exited application or signal termination permits continuation. Exit 78 denotes
invalid local configuration/security and rejects startup; other nonzero normal
exits reject startup unless explicitly classified as temporary unavailability
(exit 75). Unknown startup errors fail closed. Never catch identity failures as
application unavailability. Platform and infrastructure failure remain fatal.

Record per-application admission (`ready`, `exited`, `timeout`) in the existing
ownership state. Status rechecks actual process identity and liveness. Report
`ready` only when all admitted applications remain live, `degraded` when a subset
is live, and `unavailable` when none are live. For VITA, a missing processor or all
radios missing makes acquisition unavailable even when detector/recorder processes
are live. Process readiness is not throughput evidence. Per-stream processor and
detector summaries separately report unavailable before first valid input, stale
after two seconds without valid input, and available after fresh valid input.
Cumulative counters do not imply current progress. Authentication failure never
permits commands or fabricated data; retry attempts/backoff remain bounded.

The processor configures each radio independently for at most five seconds and
starts the configured subset on one common scheduled sample epoch. No lifecycle
change modifies VITA timestamps, transactions or packet encoding. Already started
radios continue on controller loss. UDP output and log queues remain bounded;
detector and recorder failures cannot block upstream work. No application or
container is automatically respawned. Restore failed components with explicit
whole-graph down/up (or the existing operator restart), which interrupts healthy
nodes. Individual replacement and hot rejoin are outside P4.

A stopped container can remove its own veth pair. Degraded admission must still
verify the shared bridge, other live paths, and ownership of any surviving OVS
port. It may accept only the absence of the exact stopped container's endpoint;
a replacement interface or container, bad MTU on a live path, or shared-network
failure rejects release. Diagnostic capture uses a separate host-owned mirror endpoint and must continue
when recorder delivery disappears. Capture health is checked independently; a
stopped recorder does not excuse a missing/replaced diagnostic endpoint or process.
Actual fresh-packet continuity remains a privileged acceptance assertion.
Cleanup retains the existing all-identities-before-mutation checks and never
removes another graph's resources.

Qualification combines common native lifecycle fault injection, independent real
radio/processor/detector failure tests, compiler/ownership negative tests and the
existing transactional regressions. Actual container/OVS failure acceptance needs
an authorized isolated Linux/Lima run and verified images; portable evidence does
not close that platform gate or P3's pending gates.
