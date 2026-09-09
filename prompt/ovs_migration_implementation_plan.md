# GraphX Lima and OVS migration implementation plan

**Repository:** `~/workspace/graphx-docker`  
**Decision sources:** ADR 0016 and ADR 0017  
**Baseline:** `migration_m0_baseline.md`  
**Plan status:** M0, M1, and M2 implemented and independently verified

## Objective

Migrate GraphX in place to one Linux network realization on native Linux and
macOS through Lima. OVS becomes the only GraphX-managed backend. Containers use
veth, QEMU uses TAP, and the former Docker macvlan/ipvlan driver names become
packet-verifiable semantic profiles.

Migration work packages use `M` identifiers so they do not collide with the
existing feature phases and their verification history.

## Sequence

| Phase | Scope | Principal exit gate |
|---:|---|---|
| M0 | Decisions and frozen baseline | Accepted ADRs, active M1 contracts, clean portable baseline |
| M1 | Lima macOS execution foundation | Two real Lima provision/verify/cleanup cycles |
| M2 | Configuration v2 and semantic profiles | Deterministic v1 migration and strict v2 validation |
| M3 | Generic ownership and OVS lifecycle | Identity-safe rollback, recovery, and exact cleanup |
| M4 | Container veth attachment | Restart-aware container pipeline carried only through OVS |
| M5 | Existing network-lab migration | Mixed, macvlan, ipvlan, route, and SDR labs use common realization |
| M6 | QEMU TAP attachment | Non-root QEMU TAP/OVS profile with SPAN evidence |
| M7 | Capture, faults, and diagnostics | Configuration-driven bounded capture/fault lifecycle |
| M8 | Compatibility closure | Legacy realization retired after full platform release gate |

## M0 decisions and baseline

M0 accepts Lima as the macOS Linux boundary, OVS system datapath as the only new
backend, configuration version 2 as the semantic change boundary, veth for
managed containers/namespaces, TAP for QEMU, and VM-local high-I/O state. It
does not change runtime code or configuration behavior.

## M1 Lima foundation

Create a reproducible dedicated Lima VM with rootful Docker, OVS, namespace,
veth, TAP, routing, nftables, netem, QEMU, capture, and build tools. Mount source
at `/workspace/graphx-docker`; retain privileged/high-I/O state on the VM-native
filesystem. Prove the primitives and current GraphX baseline twice with exact
cleanup. The active contracts are `prompt/implement.md` and
`prompt/verifier.md`.

## M2 Configuration version 2

Add semantic profiles (`ethernet`, `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and
`ipvlan-l3s`) and explicit attachment kinds (`container_veth`,
`namespace_veth`, `qemu_tap`, `external`, and `mirror`). Specify MAC, ARP,
broadcast/multicast, routing, isolation, and management behavior for every
profile. Retain version-1 meaning and add a deterministic, reviewable v1-to-v2
migration command. Never silently reinterpret `driver`.

## M3 Ownership and OVS lifecycle

Separate normalized planning, OVS realization, endpoint attachment, and
persistent run state. Record configuration hashes, ownership tokens, OVS UUIDs,
ifindices, namespace inodes, container identities, TAP owners, routes, rules,
qdiscs, captures, and process identities. Refuse unowned collisions and verify
identity immediately before deletion. Test interruption after every mutation
stage and preserve unrelated replacements.

## M4 Container veth attachment

Separate management connectivity from the GraphX data plane. Resolve managed
containers by verified deployment identity, create an owned veth pair, attach
the host end to OVS, move the peer into the recorded container namespace, and
apply the declared MAC, address, MTU, and routes. Detect namespace replacement
and fail closed until explicit reattachment is proven safe.

## M5 Existing laboratory migration

Migrate `mixed-network` first because it covers both profile families, routing,
policy, mirrors, and netem. Then migrate the focused macvlan, ipvlan-l2,
ipvlan-l3, static-route-policy, and SDR external examples. Run the same Linux
topology on native Linux and in Lima. Demote the privileged Docker Desktop OVS
container to a named legacy simulation during the compatibility window.

## M6 QEMU TAP and OVS

Reuse the current guest, QMP evidence, packet observer, telemetry, and GUI.
Create and own TAP in the privileged infrastructure lifecycle, grant a non-root
QEMU process access to exactly that TAP, connect it to OVS, and capture through
SPAN. Prove guest MAC identity, unicast, allowed broadcast/multicast, VLAN and
fault behavior, pause/resume, and cleanup. Retain slirp temporarily as a
compatibility profile. Do not treat Apple Silicon TCG as KVM evidence.

## M7 Capture faults and diagnostics

Make OVS observation points and fault targets declarative. Preserve separate
GraphX USER0 and Ethernet PCAPNG trust domains. Add bounded rotation, retention,
ownership, complete-block publication, and export from VM-local storage. Make
netem and later OVS port/drop faults explicit, timed, observable, and exactly
cleanable. Distinguish policy, route, link, attachment, and application failure
in telemetry and the GUI.

## M8 Compatibility and release closure

Remove Docker macvlan/ipvlan creation and Compose data-plane membership from
the active implementation after all examples migrate. Retire the Docker Desktop
simulation and QEMU slirp default only after their replacements pass the full
matrix. Retain the version-1 parser for the published compatibility window and
ship migration documentation.

## Common evidence rules

Every phase must run affected portable regression tests, use platform-specific
runtime evidence, preserve the frozen M0 record, keep resources bounded, and
write separate implementation and independent-verification reports. A dry-run,
simulation, handoff, or macOS host result cannot prove Linux kernel behavior.

The final release matrix covers macOS with a fresh Lima VM, native ARM64 Linux,
native x86_64 Linux with TCG, and supported x86_64 Linux with KVM. Completion
requires two clean lifecycle cycles, deliberate failure/recovery cases, packet
evidence for every semantic profile, and no unrelated host or guest drift.
