# GraphX implementation work package

Implement **Migration M7: declarative capture, faults, and diagnostics** in
`~/workspace/graphx-docker` after the independently verified M6 boundary.

## Objective

Bring OVS observation points and timed network faults into the identity-owned
version-2 infrastructure lifecycle. Store bounded Ethernet PCAPNG in VM-local
storage, export only complete snapshots, and distinguish failure layers in
telemetry and the GUI.

## Required implementation

- Add strict version-2 `network.captures` declarations for mirror attachments,
  VM-local directories, snap length, size/file/time rotation, and retention.
- Add strict version-2 `network.faults` declarations for realized veth/TAP
  attachments, bounded delay/jitter/loss/rate, and mandatory duration.
- Start `dumpcap` and `tc netem` only through the privileged ownership
  lifecycle. Record directory, interface, process/timer, qdisc, graph/config,
  and owner identities; status and cleanup must fail closed on replacement.
- Publish captures in owner-specific mode-0700 sessions, bound the ring, stop
  the exact process, and retain completed sessions read-only. Never combine
  Ethernet capture with GraphX LINKTYPE_USER0 application capture.
- Provide an exclusive, no-symlink export command that copies one bounded,
  complete-block PCAPNG snapshot from VM-local storage.
- Make faults self-expire, observable as active or expired, and exactly
  cleanable without touching an unrelated/replaced qdisc or timer.
- Surface policy, route, link, attachment, and application diagnostic layers in
  telemetry and the GUI while preserving the bounded evidence contract.
- Add a focused example, strict negative tests, portable contracts, and a
  privileged two-cycle Linux/Lima lifecycle regression with injected failure,
  hard-crash recovery, bounded rotation/export, expiry, and zero infrastructure
  residue. Retained read-only evidence is intentional.

## Verification

Run formatting, the full portable suite, quality/sanitizer, frozen fingerprint,
schema, migration, documentation, and prior-milestone gates. In Lima or native
Linux, run the M7 lifecycle regression twice and inspect system OVS, dumpcap,
PCAPNG, timer/qdisc identity, automatic expiry, exclusive export, recovery, and
cleanup.

Write `migration_m7_handoff.md`. Do not commit, publish, or advance to M8.
