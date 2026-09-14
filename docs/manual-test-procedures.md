# Manual acceptance checks

Run [`test-procedure.md`](test-procedure.md) first. Then perform the checks relevant
to the changed area. The [`complete user guide`](user-guide.md) defines the
commands and terminology these manual observations use.

## Portable system

1. Compile the sample pipeline using a verified image catalog and set `GX_OUTPUT`,
   `GX_STATE`, `GRAPHX_BIN` and `GRAPHX_IMAGE_RELEASE` as described in [execution](execution.md).
   Start `examples/sample-pipeline/scripts/demo.sh start`.
2. Open the printed UI URL and confirm topology, live metrics, SLO, and history.
3. Send a permitted pause/resume command and confirm the audit record.
4. Run `examples/sample-pipeline/scripts/demo.sh stop` and confirm the Compose project is gone.

## Privileged network labs

1. Enter native Linux or the GraphX Lima guest.
2. Run the example launcher and inspect `graphx run status --output "$GX_OUTPUT" --state-root "$GX_STATE" --allow-privileged`.
3. Confirm OVS ports, addresses, routes, policy, capture, and fault state expected by
   that example.
4. Stop the example and confirm only identity-owned resources were removed.

## QEMU TAP

1. Build and start the TCG-backed TAP scenario.
2. Confirm bidirectional TCP/UDP traffic, VLAN isolation, SPAN capture growth,
   packet history, and QMP runtime evidence.
3. Stop it and confirm TAP, bridge, namespace, QEMU and live ownership resources are
   removed by the owned lifecycle.
