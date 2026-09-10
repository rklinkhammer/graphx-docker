# GraphX independent verification work package

Independently verify **Migration M7: declarative capture, faults, and
diagnostics** in `~/workspace/graphx-docker`. Derive M7-001 through M7-010 from
`prompt/implement.md`, ADRs 0016/0017, and the accepted M6 report. Treat
`migration_m7_handoff.md` as a claim and write `migration_m7_verification.md`
without product fixes.

## Required checks

1. Build fresh and run portable, quality, sanitizer, fingerprint, projection,
   documentation, M2 through M6, telemetry, and web gates.
2. Prove strict capture/fault schema and parser bounds, reference checks,
   deterministic output, and version-1 compatibility.
3. Inspect the dry-run and live ledger for exact capture directory/interface,
   dumpcap PID/start time, fault interface/ifindex, qdisc, timer, owner,
   graph, and configuration identities.
4. In Lima ARM64 Linux, run the privileged M7 lifecycle regression twice,
   including deliberate create failure and hard-crash recovery.
5. Prove dumpcap observes only a declared mirror, creates Ethernet PCAPNG in a
   mode-0700 VM-local owner session, and never conflates it with GraphX USER0.
6. Drive rotation and prove file count/size/time bounds; after stop, require the
   retained session to be non-writable and the capture process to be gone.
7. Export one identity-checked complete PCAPNG snapshot, prove it is readable,
   bounded, and cannot overwrite or follow a destination symlink.
8. Prove timed netem appears as active, self-expires, reports expired, and is
   cleared exactly; replacement or identity drift must fail closed.
9. Prove telemetry/API/GUI distinguish policy, route, link, attachment, and
   application layers with state-specific evidence.
10. Destroy all owned infrastructure and prove no bridge, veth, TAP, namespace,
    qdisc, ledger, timer, or capture process remains; retained capture evidence
    must remain scoped and read-only.

M7 passes only with real system OVS, real dumpcap/PCAPNG and netem evidence, two
clean cycles, and exact identity-safe cleanup. A dry-run alone is insufficient.
