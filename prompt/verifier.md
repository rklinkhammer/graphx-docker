# GraphX independent verification work package

Independently verify **Migration M6: QEMU TAP and OVS attachment** in
`~/workspace/graphx-docker`. Derive M6-001 through M6-010 from
`prompt/implement.md`, ADR 0017, and the accepted M5 report. Treat
`migration_m6_handoff.md` as a claim and write `migration_m6_verification.md`
without product fixes.

## Required checks

1. Build fresh and run the full portable, quality, sanitizer, fingerprint,
   projection, documentation, M2, M3, M4, and M5 gates.
2. Validate the M6 configuration and migration output; prove missing, root, or
   misplaced TAP UID/GID and QEMU TAP peer/routes are rejected.
3. Inspect dry-run and live realization to prove GraphX creates system OVS and
   TAP directly, without Docker data networks, macvlan, ipvlan, or slirp.
4. In Lima ARM64 Linux, run the M6 privileged lifecycle regression twice and
   inspect TAP ifindex/alias/owner, OVS UUIDs/markers, VLAN state, ledger phase,
   failure rollback, crash recovery, replacement refusal, and exact cleanup.
5. Run the actual guest profile. Prove QMP reports TCG on Apple Silicon, QEMU's
   UID/GID is 65532, and the process can use only the declared TAP.
6. Prove guest TCP and UDP unicast, guest MAC learning, allowed broadcast and
   multicast forwarding, VLAN-42 communication, and VLAN-43 isolation.
7. Prove OVS SPAN capture grows and the packet observer produces bounded
   PCAPNG/history evidence on the VM-native filesystem.
8. Pause and resume the VM through QMP and require confirmed state transitions
   plus restored TCP/UDP readiness.
9. Exercise the explicit netem hook, remove it, stop all identity-checked
   processes, and prove no bridge, TAP, veth, namespace, qdisc, ledger, or live
   process remains.
10. Confirm both slirp profiles retain their compatibility meaning, M6 does not
    claim KVM on Apple Silicon, and declarative capture/fault ownership remains
    M7.

M6 passes only with real system-OVS, TAP, QEMU guest, and packet evidence.
