# GraphX independent verification work package

Independently verify **Migration M3: generic ownership and OVS lifecycle** in
`~/workspace/graphx-docker`. Derive M3-001 through M3-010 from
`prompt/implement.md`, ADR 0017, and the M0 baseline. Treat the handoff as a
claim and write `migration_m3_verification.md` without product fixes.

## Required checks

1. Build from a fresh out-of-tree directory and run every portable test,
   quality profile, sanitizer profile, projection check, and M0 fingerprint.
2. Confirm v1 output is unchanged and v2 dry-run contains only the M3 bridge
   ownership plan—no Docker network, port, veth, namespace, TAP, mirror, route,
   fault, capture, or process mutation.
3. In the real Lima ARM64 guest, inspect the state directory/file modes,
   configuration digest, random token, expected set, UUID records, system
   datapath, and all three bridge `external_ids`.
4. Exercise normal create/status/destroy, repeated create, missing state,
   unowned collision, missing bridge, configuration edits, malformed and
   permissive state, state-root/file symlinks, and concurrent graph locking.
5. Interrupt after every mutation using both rollback and hard-crash points.
   Prove recovery finds an unrecorded atomic mutation by marker and immediate
   retry succeeds.
6. Replace every resource between state capture and cleanup, including a
   replacement with copied markers but a different UUID. Cleanup must refuse
   before deleting any owned sibling and must preserve every replacement.
7. Inspect the deletion command: UUID and intrinsic markers must be conditions
   in the same OVSDB transaction as removal.
8. Run the retained Lima M1 verifier as a regression gate, compare unrelated
   state before/after, and return Lima to its initial state.
9. Confirm docs distinguish M3 bridge ownership from M4/M6 endpoints and later
   route/fault/capture work.
10. Report each requirement as Implemented, Partial, Missing, or Not Yet
    Applicable. M3 passes only with real OVS evidence and exact cleanup.
