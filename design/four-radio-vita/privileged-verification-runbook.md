# P3/P4 privileged verification: operator runbook

## Current readiness

The dedicated `graphx` Lima VM is running and identity matched. The user authorized
its clean recreation without backups and the full privileged P3/P4 matrix.
**P3 and P4 pass all 18 cases in Lima.** Current evidence
is summarized in [P3 verification](p3-verification.md) and
[P4 verification](p4-verification.md). No external backup selection
or repeated authorization is needed for this already authorized run.

Issue commands from the Mac and use the dedicated guest for privileged work.
OrbStack runs the unprivileged image build/smoke checks. The required provisioning
fingerprint is `995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e`.
Recheck identity before execution; do not edit the fingerprint to bypass failure.
For a new operator session, [AGENTS.md](../../AGENTS.md) still requires explicit
authorization for privileged tests; preparation alone does not grant it.

## 1. Inspect or reproduce the prepared artifacts

The preparation record, exact digests and executed nonprivileged checks are in
[P3/P4 preparation evidence](p34-preparation.md). The current local artifacts are:

```text
outputs/verification/p301-fix/images-identity/
outputs/verification/p301-fix/prepared-identity/
```

The first directory is the existing verified image-release format: OCI archives,
SPDX inventories, `images.json`, and the derived locked catalog. The second contains
an extracted, checksum-verified Linux CLI, `preparation.json` with graph IDs and
harness hashes, and reviewable authored v3 fixtures with the same verified catalog.
These are private dirty-worktree test candidates, not published P5 releases.
The Linux CLI cannot be executed natively on macOS.

These commands exist and are nonprivileged. Run from the repository root:

```sh
python3 tests/test_vita_live.py --list-cases
python3 scripts/release/image_release.py verify \
  outputs/verification/p301-fix/images-identity
cat outputs/verification/p301-fix/prepared-identity/preparation.json
```

To rebuild after a source change, choose **absent output directories**, then run:

```sh
python3 scripts/release/image_release.py build --with-vita --qualification-hooks --no-cache --allow-dirty \
  --platform linux/arm64 \
  --output outputs/verification/p34-rebuild/images
python3 tests/test_vita_live.py --prepare \
  --images outputs/verification/p34-rebuild/images \
  --output outputs/verification/p34-rebuild/prepared \
  --run-root /var/lib/graphx/verification/p34/run
```

On macOS the builder requires the ready OrbStack Docker context. It performs two
independent no-cache builds, verifies actual OCI manifests/layers, licenses, dependency
pins and SPDX inventories, then smoke-tests with no network, no capabilities and
read-only filesystems. VITA applications fail closed without configuration/NET_RAW;
the separate Linux recorder test verifies receive-descriptor restrictions without
network privileges. No image is published. The dependency remains
`dbe85d37155145842da60367af1c4beef8801b0c`.

`--qualification-hooks` deliberately enables `GRAPHX_QUALIFICATION_HOOKS` in this private
candidate. Normal CMake/Docker builds leave those hooks off. The hooks inject exit
75, exit 78 or a bounded stall **inside the real bound application before readiness**;
they also let the harness pause its exact runner child at an infrastructure mutation
checkpoint. They are not production policy or a replacement protocol runtime.

`--prepare` only verifies/extracts artifacts and writes review material. It does not
call Docker, start applications, create OVS resources or qualify a packet path.
`tests/test_vita_network.py` remains the separate portable contract test.

## 2. Recheck the Lima environment from your Mac

Before each privileged run, perform these read-only checks:

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

Stop on any nonzero result. Expected: Darwin/arm64 and the fixed, running,
identity-matched `graphx` VM. Only then run:

```sh
python3 infrastructure/lima/run-bounded.py 120 \
  limactl shell --workdir /workspace/graphx-docker graphx -- bash -lc '
    set -e
    uname -sm
    docker info
    docker compose version
    node --version
    python3 -c "import yaml"
    for tool in ip tc nft dumpcap tshark capinfos ethtool nsenter; do
      command -v "$tool" || exit 1
    done
    sudo -n ovs-vsctl --timeout=5 show
    df -h /var/lib/graphx
  '
```

Expected: Linux/aarch64, Node 24, reachable Docker/Compose and OVS, Python with
PyYAML, every listed tool and at least 10 GiB free for imports, histories and
bounded evidence. Images occupy about 683 MB before engine import. Existing
bridges are inventory, not permission to modify them. Missing packages or an
unavailable engine are failed prerequisites; request scoped repair, not a switch
to OrbStack. No QEMU guest, TCG or KVM qualification is needed for these containers.

## 3. Stage and review the exact run

After environment checks pass, stage the prepared inputs on the guest disk.
These commands copy artifacts only; they do not run acceptance or provision tools.
Use newly prepared artifacts whose run root matches the selections below. Completed
run roots cannot be reused.
The destination must not exist; do not overwrite another run.

```sh
python3 infrastructure/lima/run-bounded.py 300 \
  limactl shell --workdir /workspace/graphx-docker graphx -- bash -lc '
    set -e
    test ! -e /var/lib/graphx/verification/p34
    sudo -n mkdir -p /var/lib/graphx/verification/p34
    sudo -n cp -R outputs/verification/p34-rebuild/images \
      /var/lib/graphx/verification/p34/images
    sudo -n cp -R outputs/verification/p34-rebuild/prepared \
      /var/lib/graphx/verification/p34/prepared
    python3 scripts/release/image_release.py verify /var/lib/graphx/verification/p34/images
    /var/lib/graphx/verification/p34/prepared/bin/graphx --version
  '
```

Review `prepared/preparation.json` and these choices before authorization:

| Selection | Value |
|---|---|
| Environment | Existing identity-matched `graphx` Lima VM, Linux ARM64 |
| Verified images | `/var/lib/graphx/verification/p34/images` |
| Verified CLI | `/var/lib/graphx/verification/p34/prepared/bin/graphx` |
| Fresh run root | `/var/lib/graphx/verification/p34/run` |
| Data subnet | `10.79.0.0/24`; harness rejects an existing overlapping guest route |
| Graph identities | Deterministic `vq-…` IDs listed in `preparation.json`, including a separate sentinel |
| Management ports | Ephemeral loopback ports, selected then recorded by the authoritative compiler |
| Resources per VITA fixture | Seven applications + platform, one owned bridge, six data veth pairs, recorder mirror pair, optional independent host diagnostic mirror pair |
| Capture limits | 9022-byte snaplen, two 4 MiB files, two-second rotation, 60-second retention |
| Process/log bounds | Catalog memory/PID limits; Docker logs two 1 MiB files per application |
| Run deadline | 3300 seconds internally, 3600-second host bound including recovery margin |
| Credentials | Lab-generated, scoped per fixture; never baked into images or included in reports |

Build fresh images once per verification run; every fixture creates new containers.
Fixtures run sequentially. P3 uses transactional startup; P4 explicitly selects
available startup with a 5000 ms readiness window. The harness regenerates each
compilation through the C++ loader, then uses common `run up/status/down`. It
never edits compiled artifacts to introduce application faults. The reviewed
fixtures use port 18080 as a review value; live compilations select free ports.

The separate diagnostic mirror is derived from the authored capture and recorded
in the same expected-endpoint/ownership ledger as other resources. It uses host
namespace identity and independent OVS/veth identities. Recorder death therefore
cannot delete its delivery path. That implementation change is portable-tested;
fresh PCAPNG continuity passed in P3-07 and P4-06.

Retained history/capture volumes are named explicitly in each fixture's
`retained-history-volumes.json`. They are intentional evidence, not live workloads.
State roots, logs and sealed captures remain guest-local. The harness compares
before/after live infrastructure and preserves its independent sentinel throughout.

## 4. Authorize, execute and recover

The current user has already authorized the full matrix. For a new session without
that authorization, review the environment and selections above and obtain it:

```text
I authorize the reviewed P3/P4 privileged acceptance run in the existing,
identity-matched GraphX Lima VM, using the verified artifacts and fresh run root
in the operator runbook. Run P3 then P4 with the real applications. Preserve
unrelated workloads and the sentinel, retain bounded evidence, and perform
identity-checked cleanup on failures. Do not attach physical uplinks, forward
privileged sockets, recreate/provision the VM, publish images, start P5/P6,
push or deploy. Stop on prerequisite or ownership mismatches.
```

Only after explicit authorization, recheck VM identity and run from the Mac:

```sh
bash -c '
  source infrastructure/lima/common.sh
  graphx_lima_require_host
  test "$(graphx_lima_assert_identity "$(graphx_lima_digest)")" = Running
'
python3 infrastructure/lima/run-bounded.py 3600 \
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  python3 tests/test_vita_live.py --run --allow-privileged --target lima \
  --images /var/lib/graphx/verification/p34/images \
  --cli /var/lib/graphx/verification/p34/prepared/bin/graphx \
  --preparation /var/lib/graphx/verification/p34/prepared/preparation.json \
  --output /var/lib/graphx/verification/p34/run \
  --subnet 10.79.0.0/24
```

The harness rejects an existing output root, wrong CLI hash, image/platform
mismatch, absent prerequisites and missing privileged opt-in. It returns nonzero
on a failed case, interruption or failed cleanup. Inspect `results.json`, the
case logs, `before.json`/`after.json`, packet probes and per-fixture recovery files.
For focused reruns, repeat `--case P3-07` or another listed ID with a **fresh run
root** and regenerate the review with `--prepare --run-root NEW_ROOT`. Unselected
cases remain NOT RUN and cannot be counted toward phase closure.

A host timeout does not prove guest cleanup. Each started fixture records exact
`status` and `down` argv arrays in `recovery.json`. Read that file first:

```sh
python3 infrastructure/lima/run-bounded.py 120 \
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n cat \
  /var/lib/graphx/verification/p34/run/P3-07/graph/recovery.json
```

For that specific fixture, the corresponding existing CLI commands are:

```sh
python3 infrastructure/lima/run-bounded.py 120 \
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  /var/lib/graphx/verification/p34/prepared/bin/graphx run status \
  --output /var/lib/graphx/verification/p34/run/P3-07/graph/compiled \
  --state-root /var/lib/graphx/verification/p34/run/P3-07/graph/state \
  --credentials /var/lib/graphx/verification/p34/run/P3-07/graph/credentials \
  --images /var/lib/graphx/verification/p34/images --allow-privileged
python3 infrastructure/lima/run-bounded.py 300 \
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  /var/lib/graphx/verification/p34/prepared/bin/graphx run down \
  --output /var/lib/graphx/verification/p34/run/P3-07/graph/compiled \
  --state-root /var/lib/graphx/verification/p34/run/P3-07/graph/state \
  --credentials /var/lib/graphx/verification/p34/run/P3-07/graph/credentials \
  --images /var/lib/graphx/verification/p34/images --allow-privileged
```

Use the failed fixture's actual recovery file, not this example's selection, when
another case fails. An ownership mismatch requires investigation before further
mutation. Never use blanket container/bridge deletion, `docker system prune`, or
VM deletion. Generic `verify.sh native-linux` and `infrastructure/lima/verify.sh`
are separate broader suites and do not substitute for these VITA assertions.

## 5. P3 automated case matrix

These are the `--case` IDs accepted by `tests/test_vita_live.py`.
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

Each row is an executable case ID. The detailed
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
