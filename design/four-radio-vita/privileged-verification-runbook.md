# P3/P4 privileged verification: operator runbook

## Start here

**You do not need to implement P5/P6 or log into a separate Linux machine.** On
Apple Silicon macOS, use the dedicated `graphx` Lima VM. Issue commands from the
Mac; `limactl shell` executes the Linux portions without an interactive login.
OrbStack is not the OVS acceptance environment.

**Full P3/P4 acceptance is not runnable with one existing command yet.** Private
VITA test-image preparation and the four-radio live harness are unfinished. The
steps below distinguish commands that exist from preparation the implementor must
finish. Do not interpret successful preflight, image verification, or `run up` as
passing the acceptance cases.

| Step | Your action | Implementor/automation action | Ready now? |
|---|---|---|---|
| 1 | Review the scope and send the preparation request below | Implement private image support and the live harness; resolve capture continuity | Request can be used now; deliverables pending |
| 2 | Run or delegate read-only Lima preflight | Check identity, tools and engine; report blockers | Existing commands below |
| 3 | Review the prepared paths, exact run command, case list and cleanup plan | Supply verified artifacts and an isolated compiled fixture | Pending step 1 |
| 4 | Explicitly authorize the scoped privileged run | Execute P3, then P4, collect evidence and clean up | Pending steps 1–3 and authorization |
| 5 | Review PASS/FAIL/BLOCKED/NOT RUN results | Fix failures, rerun affected cases and update the phase records | After execution |

No privileged acceptance is authorized merely by reading this document. The
repository's [AGENTS.md](../../AGENTS.md) requires explicit authorization for
privileged Linux/Lima tests. This documentation task does not provide it.

## 1. Request the missing automation

Give the implementor this request in the GraphX workspace:

```text
Prepare automated P3/P4 privileged qualification for the current GraphX checkout.
Read AGENTS.md, docs/project-decisions.md, docs/test-procedure.md and
 design/four-radio-vita/{privileged-verification-runbook,p3-verification,
 p4-verification,network-design,lifecycle-design}.md.
Keep vrt_framework pinned to dbe85d37155145842da60367af1c4beef8801b0c.

Build the missing private test-image support and automated four-radio live harness
using the existing image-release verification, authoritative compiler, common
ownership lifecycle and guarded Linux/Lima test conventions. Do not add a separate
runtime manifest or platform-specific launcher. These are P3/P4 qualification
fixtures, not P5 publication or the final demonstration graph.

Resolve diagnostic-capture continuity against the approved contract: reporting
capture delivery unavailable after recorder death does not pass the P3 continuity
requirement. Add independent assertions; do not weaken acceptance.

Run applicable nonprivileged preparation checks. Supply the exact source revision,
verified image/binary identities, guest-local paths, bounded execution command,
case IDs, planned resources and cleanup/recovery commands. Update this runbook with
the actual automation command when it exists. Identify any missing guest packages
or VM identity mismatch. Do not initiate privileged acceptance, recreate/provision
the VM, publish images, start later phases, push or deploy.
```

Preparation must deliver all of the following before a privileged run:

- Linux ARM64 images containing `graphx-vita-radio`, `graphx-vita-processor`,
  `graphx-vita-detector` and `graphx-vita-recorder`, with the pinned library and
  runtime dependencies; a compatible platform image and verified CLI installation.
- The existing verified image-release catalog/lock and binary/SBOM/license evidence.
  Tags, `latest`, design pins and Docker config IDs substituted for manifest digests
  are not sufficient. The current `scripts/release/image_release.py` recipes list
  runtime/telemetry/SDR roles, not the VITA executable set; running that builder
  unchanged is not VITA image qualification.
- An authored v3 fixture for four radios, processor, detector and passive recorder,
  isolated owned OVS networking, lab mTLS and optional independent diagnostics.
  Use an unused graph ID, subnet and console port. Compile for `lima` through the
  authoritative loader. Regenerate catalog locks before compiling; never edit
  compiled files afterward.
- A Linux-capable harness extending existing guarded test conventions. Every
  fault must target a ledger-verified resource. It must implement the matrices
  below, impose deadlines, save failures, and attempt owned cleanup on interruption.
  Timing-sensitive cases must not overlap compilation or other traffic suites.
- A separately owned sentinel workload and before/after resource inventories.
  Infrastructure creation/deletion goes through the common lifecycle; no hidden
  setup script, direct `docker compose up` substitute or broad cleanup is acceptable.
- Exact executable commands with actual paths, not unspecified `$P3_*` variables.
  The supplied summary must also state the required privilege, storage budget,
  overall timeout and behavior when a case or cleanup fails.

`tests/test_vita_network.py BUILD --retain DIRECTORY` is a **nonprivileged contract
fixture generator**, not the missing live harness. Its fixed names, addresses and
design catalog need adaptation for isolated live execution. Likewise,
`tests/test_ovs_execution_live.py` currently selects existing generic network cases;
it does not have a four-radio VITA acceptance case.

## 2. Check the Lima environment from your Mac

These are read-only commands. Run from the GraphX repository root:

```sh
cd /Users/rklinkhammer/workspace/graphx-docker
uname -sm
command -v limactl
limactl list graphx
bash -c '
  source infrastructure/lima/common.sh
  graphx_lima_require_host
  test "$(graphx_lima_assert_identity "$(graphx_lima_digest)")" = Running
'
```

Expected: Darwin/arm64, Lima installed, and the fixed `graphx` VM running with
matching repository/configuration identity. An empty instance list, stopped VM or
nonzero identity check blocks the next step.

Only after that check succeeds:

```sh
python3 infrastructure/lima/run-bounded.py 120 \
  limactl shell --workdir /workspace/graphx-docker graphx -- bash -lc '
    set -e
    uname -sm
    docker info
    docker compose version
    node --version
    for tool in ip tc nft dumpcap tshark capinfos ethtool; do
      command -v "$tool" || exit 1
    done
    sudo -n ovs-vsctl --timeout=5 show
  '
```

Expected: Linux/aarch64, reachable guest Docker/Compose, Node 24, all listed tools,
and a successful OVS query. Existing bridges are inventory, not permission to
modify them. The host's Docker context need not be changed or forwarded.

| Failure | Your next step |
|---|---|
| Lima missing | Request/install the repository-supported Lima version before continuing. |
| VM absent or stopped | Explicitly authorize environment creation/start if wanted. `build/dev/graphx env up` is the existing environment command once that CLI is built; it can create/start the VM and is not a read-only test command. |
| VM identity/configuration mismatch | Stop and request a separate recovery proposal. Starting again does not repair stale identity. Do not delete/recreate the VM, edit fingerprints or bypass checks to make tests run. |
| Missing `ethtool` or other guest prerequisite | Request a scoped guest-environment repair plan. Changing `provision.sh` does not install packages into an existing guest. Recheck identity after any approved environment change. |
| Docker/OVS unavailable or sudo refused | Report the exact preflight failure; repair the selected guest before testing. Do not switch the tests to OrbStack. |

The preflight is a procedure, not a claim that this VM has been checked today.
No environment setup or repair is authorized by these instructions alone.

## 3. Review the prepared run before authorization

The implementor must fill in these values in a preparation summary:

| Value | Required meaning |
|---|---|
| Source revision | Exact GraphX commit and clean/dirty status; exact pinned vrt_framework revision |
| Environment | Host/guest architecture, Lima instance identity and required packages |
| Run root | Fresh absolute guest path under `/var/lib/graphx/verification/`; never reuse an unrelated run |
| Release and images | Absolute guest paths to the verified installation and image-release catalog |
| P3 / P4 compiled roots | Separate compilations; P3 transactional, P4 explicitly available |
| State / credentials | Exact guest-local roots used by those compilations and runtime invocations |
| Resources | Graph IDs, bridge, subnet, ports, owned container roles and sentinel identity |
| Run / cleanup commands | Actual harness command, timeout, and exact `run status`/`run down` recovery commands |
| Evidence | Case results, packet observations, logs/counters and before/after inventories |

Credentials are generated through the existing credential mechanism; never embed
private keys in images or the report. The host path
`/Users/rklinkhammer/workspace/vrt_framework` is not automatically a guest path:
preparation must stage/verify the pinned sources or artifacts on the guest disk.

**Capture continuity is an open implementation/acceptance issue.** P3 requires
independent diagnostics to continue during recorder failure. P4 currently describes
loss of mirror delivery when the recorder's stopped namespace removes its veth.
Retained old packets or an honest `unavailable` status do not establish continuity.
The implementor must preserve the approved behavior or report a concrete blocker;
a documentation edit alone cannot turn this case into PASS.

## 4. Authorize and execute the automated run

After reviewing the preparation summary, you can send:

```text
I authorize execution of the reviewed P3/P4 privileged acceptance plan in the
existing, identity-matched GraphX Lima VM. Use only the listed isolated test
resources under /var/lib/graphx. Run P3 first, then P4, with the actual applications.
Preserve existing workloads and the sentinel. Collect evidence and perform
identity-checked cleanup even on failures. Do not attach physical uplinks, expose
privileged sockets, recreate/provision the VM, publish images, start P5/P6, push
or deploy. Report any prerequisite or ownership mismatch instead of bypassing it.
```

The implementor can then run the prepared harness from the Mac through the existing
bounded Lima execution machinery. You do not need an interactive guest shell.
There is deliberately no invented `--p3`/`--p4` command here: the missing harness
must supply and test its actual entry point in step 1.

For troubleshooting, the underlying CLI already supports the following template.
These commands manage **one prepared graph only**; they are not acceptance tests.
Replace every value with the exact guest path from the preparation summary before
using them, and use the matching P3 or P4 compilation/state/credentials together:

```sh
# Variables are set in your Mac terminal; every value identifies a guest path.
GX_VITA_RELEASE='/var/lib/graphx/REPLACE_WITH_VERIFIED_INSTALLATION'
GX_VITA_IMAGES='/var/lib/graphx/REPLACE_WITH_VERIFIED_IMAGE_RELEASE'
GX_VITA_COMPILED='/var/lib/graphx/REPLACE_WITH_PREPARED_COMPILED_ROOT'
GX_VITA_STATE='/var/lib/graphx/REPLACE_WITH_THIS_RUN_STATE_ROOT'
GX_VITA_CREDENTIALS='/var/lib/graphx/REPLACE_WITH_THIS_RUN_CREDENTIAL_ROOT'

# Status: read the recorded identity and health of this prepared run.
python3 infrastructure/lima/run-bounded.py 120 \
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  "$GX_VITA_RELEASE/bin/graphx" run status \
  --output "$GX_VITA_COMPILED" --state-root "$GX_VITA_STATE" \
  --images "$GX_VITA_IMAGES" --release "$GX_VITA_RELEASE" \
  --credentials "$GX_VITA_CREDENTIALS" --allow-privileged

# Recovery/stop: only after the scoped run is authorized.
python3 infrastructure/lima/run-bounded.py 300 \
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  "$GX_VITA_RELEASE/bin/graphx" run down \
  --output "$GX_VITA_COMPILED" --state-root "$GX_VITA_STATE" \
  --images "$GX_VITA_IMAGES" --release "$GX_VITA_RELEASE" \
  --credentials "$GX_VITA_CREDENTIALS" --allow-privileged
```

The harness uses `run up` with the same selection after verifying prerequisites
and saving the baseline inventory. Recheck VM identity before an invocation; direct
`limactl shell` alone does not perform the repository's VM fingerprint check.
A timeout does not prove guest resources were removed: inspect with the same
selection and perform owned recovery. Never use `docker system prune`, wildcard
container deletion, blanket `ovs-vsctl del-br`, or VM deletion as cleanup.

Do not substitute `scripts/verify.sh full`, `scripts/verify.sh native-linux`, or
`infrastructure/lima/verify.sh` for this harness. The latter two are broader
privileged suites, including existing network/guest cases and their artifact
prerequisites; they do not currently assert all P3/P4 VITA criteria. QEMU/TCG/KVM
qualification is not needed to prove this container-only four-radio path.

## 5. P3 automated case matrix

Each row is a required harness case, **not an existing test name or CLI flag**.
Use fresh isolated fixtures for destructive/negative cases. The detailed probe
requirements remain in [P3 case specifications](p3-verification.md#p3-live-case-specifications).

| Case ID | Automated action | PASS evidence |
|---|---|---|
| P3-01 baseline | Start the real seven applications through the common CLI with transactional policy | All local readiness, correct images/namespace identities, release token, observed radio/spectrum/recorder progress |
| P3-02 MTU | Inspect host/peer/application interfaces, OVS MTU and offloads; test lowered MTU separately | 9000-byte target throughout, TSO/GSO/GRO/TX checksum offloads disabled; insufficient path rejects release |
| P3-03 jumbo bytes | Send DF-set UDP payloads of 4128 and 8836 bytes over the owned application paths | Exact source/destination bytes; independent observation finds no fragments; complete mirror frames |
| P3-04 selection | Exercise Data, Context, encrypted TCP control/replies, spectra and ARP; send numbered probes and sentinel/management traffic | All expected families mirrored both directions; one copy per forwarding instance; unrelated/management frames absent |
| P3-05 passive endpoint | Inspect capabilities/seccomp/mounts and attempt scoped injection with test probes | Recorder capabilities zero after setup; recorder descriptor transmit denied; separate NET_RAW probe dropped by host tc, absent at application receivers |
| P3-06 saturation | Run actual four radios and maximum FFT geometry for 60 seconds; stall recorder receive/log consumption | Bounded RSS/socket/log storage, available drop counters, healthy detector progress, bounded recorder stop; no recorder archives |
| P3-07 independent capture | Repeat capture off/on; kill recorder with capture enabled | Recorder counts independent of capture; full frames, configured retention limits and fresh diagnostic packets after recorder failure; loss of delivery is FAIL/BLOCKED, not PASS |
| P3-08 identity/interruption | Interrupt registered mutations and peer movement; introduce controlled namespace/ifindex/alias replacements | Recovery from ledger; substituted resources rejected without deletion; no application release on unsafe state |
| P3-09 cleanup | Run owned down after success/failure; compare inventories and sentinel | No new live owned containers, networks, OVS resources, veth/namespaces, qdiscs or capture processes; unrelated resources unchanged; only explicitly retained evidence/history/artifacts remain |

Wire accounting must distinguish UDP payload, IP packet, Ethernet/VLAN bytes and
capture snap length. Require snaplen at least 9022 for the 9000-byte MTU contract.
The nominal IQ payload is 16 MB/s; report actual source, wire and recorder counts
separately. Best-effort recorder reception is not a lossless throughput assertion.

## 6. P4 automated case matrix

Use the same verified application set and isolated topology with this authored
selection, then recompile rather than editing normalized output:

```yaml
lifecycle:
  startup: available
  readiness_ms: 5000
```

Every row is a required harness case, not an existing command. The detailed
contract is in [P4 case specifications](p4-verification.md#p4-live-case-specifications).

| Case ID | Automated action | PASS evidence |
|---|---|---|
| P4-01 baseline | Start all seven apps with available policy | Correct admission records and release; actual packet/spectrum/recorder progress |
| P4-02 pre-readiness matrix | For each radio1–4, processor, detector and recorder: induce safe application exit and readiness stall after network preparation but before local readiness | Each of 14 cases records bounded disposition; healthy subset released; timed-out apps stopped before release; no late admission or respawn |
| P4-03 unsafe startup | Exercise invalid config/credentials, release token, image identity, MTU/ACL and substituted container/interface identities in separate fixtures | Release refused and owned rollback/recovery; failures never reclassified as ordinary peer unavailability |
| P4-04 runtime matrix | Kill each of seven application containers separately after observed progress | Healthy streams continue; processor death leaves started radios transmitting and detector stale; detector/recorder death does not stop upstream; container IDs/RestartCount prove no respawn |
| P4-05 unavailable dependencies | Start with all radios unavailable, and separately with processor unavailable | No useful spectra claim in first case; no initial radio configuration/acquisition in second; surviving components remain responsive |
| P4-06 recorder/capture | Repeat recorder failure with diagnostic capture disabled and enabled | Upstream unaffected, passive attachment preserved; enabled diagnostics meet P3-07 continuity, not merely retained-file existence |
| P4-07 shared OVS loss | Remove only the test-owned shared bridge during a scoped fault case | Responsive bounded survivors, unavailable/stale functions, no fabricated throughput, no automatic infrastructure repair |
| P4-08 restore/interrupt | Interrupt startup/shutdown; restore with explicit whole-graph down/up | Bounded owned recovery; healthy nodes interrupted only by deliberate whole-graph restart; no individual replacement/hot-rejoin claim |
| P4-09 cleanup | Compare final inventories for every case | Same cleanup and sentinel requirements as P3-09, with logs/failed admission evidence retained |

Distinguish failure of an application after infrastructure preparation from failure
of the pre-application network holder. The latter is an infrastructure prerequisite
failure and must not be accepted as degraded application admission. Fault timing
must be synchronized to observed lifecycle events, not guessed with sleeps.

## 7. Read the report and decide closure

Require a result for every case and subcase: **PASS**, **FAIL** (assertion failed),
**BLOCKED** (named prerequisite/implementation issue), or **NOT RUN**. An overall
success exit is permitted only when all requested cases pass and cleanup passes.

The report must contain exact commands, commit/pin/image digests, environment and
package versions, timestamps/durations, observed resource bounds, packet evidence,
application counters, and before/after ownership/infrastructure inventories. Copy
only a small summary to the Mac if desired; high-I/O artifacts remain in the guest.
Do not include credential secrets in the summary.

P3 closes only when P3-01 through P3-09 pass. P4 closes only when its entire matrix
passes with the required P3 path evidence. If a prerequisite, capture-continuity
fix, test-harness implementation or cleanup check remains outstanding, record it
explicitly and leave the affected phase open. Neither authorization nor a successful
`run up` is an acceptance result.
