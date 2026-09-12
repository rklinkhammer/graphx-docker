# QEMU TAP/OVS profile

`graphx.yaml` declares the QEMU TAP, namespace peer, access VLANs, mirror, and
external raw TCP/UDP edges. `scripts/ovs-lab.sh` realizes the lab, starts the guest
and observer, records QMP evidence, and cleans up through identity-checked GraphX
infrastructure operations. The declarative `network.captures` entry owns `dumpcap`;
the observer consumes bounded atomic PCAPNG exports through a root-owned handoff
directory. It never reads the live capture ring and no second packet-capture process
is started. QEMU user networking is not used.
