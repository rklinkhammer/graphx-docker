# P7 verification

P7 implements compiled OVS resource preparation through the finite graph runner.
**S07–S12 privileged acceptance passed in the existing GraphX Lima VM with explicit user authorization.**
Network profile and static-route startup wrappers use the compiled runner.
Native Linux OVS is not claimed.

The authoritative resolved network now projects into the existing bridge,
endpoint, namespace, profile-flow and capture modules. Logical switch references
map to hashed names; router interfaces expand once; diagnostic namespaces share
the namespace owner; address aliases survive through realization. A unicast
connection selecting a declared destination alias binds that alias. Deferred
router routes remain absent at baseline, and scenario faults never run implicitly.

`graphx run plan --output COMPILED --state-root STATE` verifies the compilation
inventory and prints the resource plan without invoking Docker, OVS or Linux
networking tools, or creating state. The plan uses the same typed projection as
realization. Compilation advertises execution support for the implemented OVS
cases, independently of whether release image identities are verified.

`graphx run up --output COMPILED --state-root STATE --images IMAGE_RELEASE
--allow-privileged` uses the existing graph lock and `STATE/GRAPH/ownership.yml`.
It requires an explicitly selected Linux/Lima target, root and a local root-owned
Unix Docker socket. Container IDs, image identities, graph/owner labels and local
cgroup membership are checked before namespace access. There is no second OVS
ledger. Namespace diagnostic applications additionally require `--release` with a
verified native installation containing `graphx-diagnostic`.

Application containers wait for an owner-token network preparation barrier before
executing their applications. The coordinator installs owned data interfaces,
profile flows and management ACLs before publishing that barrier. The ACL permits
only the resolved platform console TCP port and telemetry UDP port on management,
allows established return traffic, and drops other management traffic and
forwarding. Application capabilities remain dropped. Router policy order remains
established/related, authored rules, default drop. Saved nft policy fingerprints
ignore changing counters and kernel handles but detect policy replacement.

Namespace diagnostics bind raw UDP listeners, report local readiness, wait for the
common release barrier and accept bounded GXR1 probe records without echoing or
forwarding them. Native process startup confirms exec after durable registration;
namespace entry checks the owned inode. Capture startup uses that same process
registration gate. Capture file limits do not constrain unrelated native platform
history files.

The owned capture exporter periodically copies complete, bounded Ethernet PCAPNG
snapshots into a fresh handoff generation mounted read-only by the platform. It
records directory and snapshot inode identities in the same ledger. Snapshots
are sealed to mode 0444 and replaced through a recorded pending inode. Interrupted
exports retain evidence rather than delete unknown files. Stopping the graph
stops applications and diagnostic/export processes, removes owned infrastructure
then deletes Compose objects, and retains sealed captures, logs and history volumes.

The final successful logs are [portable](../../outputs/verification/20260913T212741Z-portable.log),
[macOS quality](../../outputs/verification/20260913T212742Z-quality.log), and
[Linux ARM64 quality](../../outputs/p7-wrapper-linux-quality/quality-20260913T212827Z.log).
[Docker inventory evidence](../../outputs/p7-linux-quality/inventory-result.json)
records zero containers before and after the verifier. Privileged acceptance ran separately in the existing `graphx` Lima VM, as recorded below.

## Accepted restriction

S14 physical-device startup remains gated. The user selected a separate future
physical-uplink ownership contract: an external radio address is not sufficient
to attach a host physical interface. The laboratory simulator is an explicit P9
scenario and is not substituted automatically. S14's resource plan is covered;
its physical execution is not claimed. Owned external S11 TAP attachment remains
separate from QEMU guest boot, which belongs to P8.

## Checks

- `python3 tests/test_ovs_compiled.py build/dev/graphx .`: passed for S07–S12/S14;
  hashed resources, single router expansion, aliases, ordered policies, deferred
  routes, no baseline fault, immutable compilation and no-side-effect privilege gate.
- `python3 tests/test_diagnostic_bindings.py build/dev .`: passed using unprivileged
  loopback sockets; local readiness, release, malformed/oversized datagrams and stop.
- Compiler verification: 63 positive target packages, 15 negative cases including
  N01/N08/N11, 33 unsupported targets, deterministic goldens and publication checks passed.
- `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick`: 35/35 passed.
- `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality`: passed
  with LLVM 21 clang-format, clang-tidy and cppcheck on macOS.
- `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable`: passed;
  includes the 35-test CTest suite, telemetry HTTP/normalized consumers, web
  tests/build and native startup, interruption and cleanup verification.
- `GRAPHX_LINUX_VERIFIER_IMAGE=graphx-p7-linux-verifier:local
  GRAPHX_LINUX_EVIDENCE_DIR="$PWD/outputs/p7-linux-quality"
  scripts/test-linux-container.sh quality`: passed in unprivileged OrbStack
  Linux ARM64; this compiles the Linux-specific namespace/process code and runs
  clang-tidy/cppcheck. Container inventories were empty before and after. No
  claim of preserving a running sentinel or privileged Linux runtime is made.
- `PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 tests/test_package.py . build/dev`:
  passed native installation, external consumer linking and exact archive validation,
  including the diagnostic executable; see [package log](../../outputs/p7-linux-quality/native-package.log).
- `/tmp/graphx-design-validation-venv/bin/python design/graph-generation/check_package.py`:
  static design package passed (24 positive cases, 15 negatives, 63 target sets,
  865 parsed files and 536 relative links); no runtime evidence is inferred.
- ShellCheck passed for the converted wrappers and shared runtime helper (exact command below).

## Privileged Lima acceptance

S07–S12 passed in the existing ARM64 Ubuntu GraphX Lima VM. The guest-local source,
build, releases and full runtime evidence remain under
`/var/lib/graphx/runtime/p7-acceptance`. No VM provisioning, Docker context change,
physical NIC attachment or privileged socket forwarding was performed.
Native Linux OVS, QEMU TCG/KVM boot and browser acceptance were not run.

The acceptance verifies these runtime boundaries: explicit standard-library
includes for the pinned Debian compiler; Docker classic/config-digest versus
containerd/manifest-digest image identities; readable non-secret configuration copies
for UID 65532 containers; and bounded status/stop lock retries during capture exports.
The preparation shell escapes Compose dollar interpolation and traps termination.
Original compiled packages remain private and unchanged. Shared-image export now
requests OCI media types and uncompressed layers explicitly, while deterministic
archive normalization and repeat-build verification remain mandatory.

The [collected results](../../outputs/p7-lima-acceptance/summary.json) retain successful
runs and incomplete attempts separately. Final profile results are below; each
verifies exact container/network/bridge/namespace inventories, host link names,
diagnostic/capture process identities and preservation of existing volumes.

| Case | Final result | Rollback points | Abrupt-exit recovery points |
|---|---|---:|---:|
| S07 MACVLAN | [Pass](../../outputs/p7-lima-acceptance/macvlan-complete/results.json) | 4/4 | 4/4 |
| S08 IPVLAN L2 | [Pass](../../outputs/p7-lima-acceptance/ipvlan-l2-complete/results.json) | 16/16 | 16/16 |
| S09 IPVLAN L3 | [Pass](../../outputs/p7-lima-acceptance/ipvlan-l3-complete/results.json) | 4/4 | first bridge |
| S10 Mixed network | [Pass](../../outputs/p7-lima-acceptance/mixed-network-complete/results.json) | 12/12 | first bridge |
| S11 Network observation | [Pass](../../outputs/p7-lima-acceptance/network-observability-release/results.json) | 4/4 | 4/4 |
| S12 Static-route policy | [Pass](../../outputs/p7-lima-acceptance/static-route-policy-complete/results.json) | 19/19 | first bridge |

Container profiles deliver samples through every application, reject a listening
but disallowed platform port, preserve management readiness after data-route removal,
and reject an explicit peer route through management. Capture cases decode nonempty
Ethernet PCAPNG snapshots with `capinfos`. S11 injects 1,000 padded ARP frames through
the owned TAP into a 64 KiB/two-file ring and verifies the retained file bound.
S12 observes allowed and denied packets and verifies the deferred route is absent
before explicit application. No scenario action executes at baseline.

The [final guest inventory](../../outputs/p7-lima-acceptance/final-inventory.json)
contains zero containers, OVS bridges or namespaces. Owned history volumes, sealed
captures, candidate images and logs remain as evidence; no broad pruning was used.
The [full-session preservation check and wrapper results](../../outputs/p7-lima-acceptance/preservation.json)
compare the first inventory with the final one, including host links and processes.
The VM had no pre-existing containers, so preservation of a running sentinel is
not claimed. The converted wrapper commands are verified below.

The guarded acceptance entry point is `tests/test_ovs_execution_live.py`. After
separate explicit authorization, use an existing ready Linux environment and
verified image release; it does not provision a VM or change Docker contexts:

```sh
python3 tests/test_ovs_execution_live.py --allow-privileged --target lima \
  --images /var/lib/graphx/runtime/p7-acceptance/images-store \
  --output /var/lib/graphx/verification/P7-UNIQUE --case macvlan --all-interruptions
```

Use `--target native-linux` for native Linux. Select `ipvlan-l2`, `ipvlan-l3`,
`mixed-network`, `network-observability`, and `static-route-policy` separately;
S12 also needs `--release /var/lib/graphx/releases/NATIVE`. The harness records
pre/post inventories, rejects corrupted bridge ownership before cleanup, checks
S12 policy and manual-route traffic, checks management after data-route removal,
checks a listening but disallowed management port, and checks sealed capture
publication where configured. It injects a first-bridge interruption by default;
`--all-interruptions` covers all registered infrastructure mutation points, and
`--crash-interruptions` selects immediate process exit followed by durable recovery. It retains its evidence
and owned history/capture volumes. The Lima runs verify application delivery,
management isolation, capture decoding, route/policy behavior and interruption cleanup.
These results do not establish native Linux or guest-boot acceptance.

The MACVLAN, IPVLAN L2/L3, mixed-network and static-route wrappers passed live
`up`, `status` and `down` against existing compiled packages and verified releases.
They contain no per-profile wiring. The macOS dispatcher has static tests for
missing compilation, missing privilege opt-in, VM identity and forwarded guest paths;
the privileged wrapper executions themselves ran inside Lima. Static-route scenario
`apply-route`/`clear-route` commands still reject execution until P9.

A Linux exit-race regression passed: process exit state is read after executable
lookup, preventing an exiting exporter from appearing to be a substituted process.
Cleanup still rejects a live mismatched identity. The wrapper checks exercised
capture-exporter and diagnostic shutdown with this fix.

Exact ShellCheck command (passed):

```sh
shellcheck -x -P SCRIPTDIR scripts/network-lab.sh scripts/lib/demo-runtime.sh \
  examples/network-lab-ovs.sh \
  examples/{macvlan,ipvlan-l2,ipvlan-l3,mixed-network}/scripts/{up,status,down}.sh \
  examples/static-route-policy/scripts/{demo,inspect}.sh
```

The guest candidates were built with `--allow-dirty`; they are local verification
artifacts, not publishable releases. The coordinator used the guest-local development CLI; the verified native installation
supplied namespace diagnostics, and verified shared images supplied container applications.
The native CTest build passed 37 tests, and the
shared images passed deterministic repeat-build, offline inventory and non-root
smoke checks. Final macOS portable and quality checks and Linux ARM64 quality passed.
A telemetry readiness assertion returned 503 during one portable run under concurrent
build load; its focused rerun and the full portable rerun passed.

The approval requirement comes from repository [AGENTS.md](../../AGENTS.md):
“Run privileged tests only on native Linux or in the GraphX Lima guest with
explicit authorization.” The [implementation plan](implementation-plan.md) also
requires separately authorized Linux and Lima acceptance before wrapper removal.
