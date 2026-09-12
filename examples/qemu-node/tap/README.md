# QEMU TAP/OVS profile

`graphx.yaml` declares the QEMU TAP, namespace peer, access VLANs, mirror, and
external raw TCP/UDP edges. `scripts/ovs-lab.sh` realizes the lab, starts the guest
and observer, records QMP evidence, and cleans up through identity-checked GraphX
infrastructure operations. The declarative `network.captures` entry owns `dumpcap`;
the observer consumes bounded atomic PCAPNG exports through a root-owned handoff
directory. It never reads the live capture ring and no second packet-capture process
is started. QEMU user networking is not used.

Platform: native Linux or the ARM64 GraphX Lima guest on Apple Silicon macOS.
The checked-in launcher always uses an x86_64 guest with TCG; it does not select
KVM. Build instructions and the `start/status/verify/stop` workflow are in the
[parent guide](../README.md) and [Lima guide](../../../infrastructure/lima/README.md).
The privileged TAP lifecycle CTest does not boot this guest; use `demo.sh verify`
after starting the full example to verify QMP, traffic, capture, and packet history.
