# QEMU TAP/OVS profile

`graphx.yaml` declares the QEMU TAP, namespace peer, access VLANs, mirror, and
external raw TCP/UDP edges. `scripts/ovs-lab.sh` realizes the lab, starts the guest
and observer, records QMP evidence, and cleans up through identity-checked GraphX
infrastructure operations. QEMU user networking is not used.
