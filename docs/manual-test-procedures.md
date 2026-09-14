# Manual acceptance checks

Run [`test-procedure.md`](test-procedure.md) first. Then perform the checks relevant
to the changed area. The [`complete user guide`](user-guide.md) defines the
commands and terminology these manual observations use.

## Portable system

1. Run `graphx example up sample-pipeline --control generator:pause,resume`.
2. Open the printed console URL and enter both tokens; confirm live metrics and history.
3. Send permitted pause/resume commands and confirm the audit record.
4. Run `graphx example down sample-pipeline` and confirm its owned containers are gone.

## Privileged network labs

1. Select an example with `graphx example plan NAME`; macOS selects Lima for OVS.
2. Run `graphx example up NAME --allow-privileged`, then `graphx example status NAME --allow-privileged`.
3. Confirm OVS ports, addresses, routes, policy, capture, and fault state expected by
   that example.
4. Run `graphx example down NAME --allow-privileged` and confirm only owned resources were removed.

## QEMU TAP

1. Run `graphx example up qemu-node/tap --allow-privileged`.
2. Confirm bidirectional TCP/UDP traffic, VLAN isolation, SPAN capture growth,
   packet history, and QMP runtime evidence.
3. Stop it and confirm TAP, bridge, namespace, QEMU and live ownership resources are
   removed by the owned lifecycle.
