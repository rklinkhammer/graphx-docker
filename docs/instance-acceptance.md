# Two-instance acceptance

`scripts/verify.sh instances` builds current images and runs the two-source SDR
graph twice concurrently through the shared instance launcher. Each copy retains
identical graph/node IDs, sample/control ports, and source settings. Instance IDs,
execution IDs, credentials, management resources, history, and console ports are
separate. Existing deployments are preserved.

The two controllers declare `control: origin`. Their authenticated pause/resume
commands control the corresponding simulated device through mutual TLS. Sources
remain `control: none`; raw sample packets carry the device protocol unchanged.
Controller output proves receipt of samples at 100 MHz and 200 MHz in each copy.

## Running the proof

On macOS, select a ready OrbStack engine, then run from the repository root:

```sh
scripts/verify.sh instances
```

This profile runs the development CTests, builds runtime/telemetry/SDR images, and
writes a timestamped log and JSON evidence under `outputs/verification/`.
It does not use or stop the root compatibility demo. For current prebuilt images:

```sh
python3 tests/test_two_instance_sdr_live.py \
  --evidence outputs/verification/two-instance-sdr-orbstack.json
```

On a prepared GraphX Lima guest, use the guest CLI and guest-native evidence path:

```sh
limactl shell --workdir /workspace/graphx-docker graphx -- sudo env \
  GRAPHX_BIN=/var/lib/graphx/runtime/build/dev/graphx \
  python3 tests/test_two_instance_sdr_live.py --build \
  --environment lima \
  --evidence /var/lib/graphx/runtime/evidence/two-instance-sdr.json
```

On native Linux, run the same Python test with `--environment native-linux`, a
Linux `GRAPHX_BIN`, a ready Docker engine, and permission to create temporary state
under `/var/lib/graphx/runtime`. This container-only proof does not authorize
privileged OVS testing. A Linux run without an explicit environment label is
reported as `linux-docker`, not native Linux or Lima acceptance.

## Assertions

| Boundary | Required evidence |
|---|---|
| Concurrent deployment | Both copies process samples; matching logical topology, distinct container/network IDs, eight distinct node credentials, distinct executions |
| Duplicate start | Refusal without changing live registrations |
| Authorization | B's operator token cannot control A; A rejects a target scoped to B |
| Device control | Acknowledged pause stops A's selected source while B advances; resume restores A's traffic |
| Node replacement | Fresh execution for the selected controller/source, unchanged peer registrations, continued traffic |
| Stale commands | Old execution targets and reused idempotency keys with changed executions are rejected |
| Observation attribution | Valid signed positive control reaches history; stale executions, wrong instance scope, and another instance's credential do not |
| Collector restart | Live observations resume without assigning new node executions |
| Isolated shutdown | B's container IDs, start times, network IDs, credentials, executions, and traffic survive A's shutdown |
| Interrupted startup | The launcher is killed with SIGKILL after recorded containers start but before application gates open; duplicate up refuses takeover |
| Replacement during recovery | Same-project container with a different immutable ID causes cleanup refusal; B is preserved |
| Recovery | Remove only the test-created replacement, then use normal checked down/up; fresh executions cannot accept pre-interruption targets |
| History | Integrity check passes; database metadata and retained rows match their instance and preserve original execution attribution |
| Cleanup | No containers, management networks, or active runtime receipts remain for either test project |

The interruption is injected only in the test child by intercepting a normal
launcher call. There is no production failpoint. Recovery uses the existing
`status`, `down`, and `up` commands; it does not introduce another deployment
manifest or bypass identity checks. The proof exercises a durable receipt boundary.
A kill between Docker creation and receipt publication still fails closed on
unrecorded objects and requires investigation.

History inspection runs inside the same Linux Docker engine as the writer and
closes its SQLite handle before reactivation. Do not inspect an OrbStack bind-mounted
live history database with host SQLite while a guest writer can be active.

The test writes machine-readable evidence with host/engine identity, configuration
digest, checks, history counts, and cleanup status. Failure diagnostics are bounded;
credentials are never included in command arguments or evidence. Failed runs retain
their temporary runtime directory for investigation, even when resource cleanup
succeeds. Passing runs remove temporary runtime fixtures and retain the report/log.

## Evidence limits

OrbStack and Lima runs are reported separately. This proof exercises simulated
SDRs and trusted adapter sessions; it does not authenticate physical hardware.
The raw SDR profile has no packet-capture provider, so this proof makes no packet
capture claim. Native application capture and OVS capture retain their own tests.

The proof creates no OVS bridges, veth/TAP devices, routes, faults, or QEMU guests.
Run `infrastructure/lima/verify.sh` separately for authorized OVS isolation and
interruption coverage. Native Linux, Lima, TCG boot, and KVM remain distinct
acceptance results; neither this proof nor TAP lifecycle checks establish guest boot.
