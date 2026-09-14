# QEMU TAP profile

`graphx.yml` declares a `guest.echo` node, a namespace `diagnostic.peer`, an
owned OVS TAP and VLAN capture. The fixed guest application obtains all TCP/UDP
addresses and ports from its resolved node bindings.

Use the [common guest release and runtime instructions](../README.md).
`scripts/ovs-lab.sh plan|up|status|down` consumes the existing `GX_OUTPUT`
compilation and common ownership ledger. It has no independent QEMU argv, PID
file, peer daemon or capture-export process. The S11 external endpoint remains
external; this profile does not attach to or adopt it.

Live acceptance requires separately authorized native Linux or GraphX Lima
execution. `tests/test_guest_execution_live.py --case S15` checks actual TCG boot
and bidirectional TCP/UDP. Explicit scenario actions verify the declared traffic, VLAN, capture and QMP expectations.

Select `graphx scenario run --action verify-guest --output COMPILED --state-root
STATE --release RELEASE --allow-privileged` for the declared bounded unicast TCP/UDP,
VLAN isolation, capture and QMP checks. Guest broadcast echo and multicast reception
are not part of the current guest contract. See [scenario execution](../../../docs/scenarios.md).
