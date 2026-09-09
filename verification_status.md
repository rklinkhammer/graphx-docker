# GraphX verification status

Review date: 2026-09-07
Reviewed repository: `/home/rklinkhammer/workspace/graphx-docker`
Reviewed branch: `main`

## Summary

Phases 3 through 12 have checked-in independent verification reports with an
`ACCEPTED` current verdict. Their Linux acceptance appendices close the earlier
platform-specific gaps. Phases 5, 11, and 12 retain their original unsuccessful
or environmentally blocked narratives as historical evidence; explicit
supersession notices prevent those narratives from being mistaken for current
status.

Phase 13 has an independent verification report with passing portable macOS and
Linux evidence. Linux-host implementation closed the portable bind-mount,
native source-PCAP ownership, and host-Python TLS readiness failures. Two
portable and two native OVS/SPAN cycles passed, followed by the complete Linux
verification profile, so Phase 13 is part of the accepted chain.

The updated repository at `9d60238` was revalidated on macOS after the Linux
results were checked in. Portable, focused SDR, GUI/browser, default and opt-out
lifecycle, capture/history, adverse-port, quality, sanitizer, and fuzz checks
all passed. Native Linux evidence was accepted from the checked-in operator run
and was not misrepresented as having been rerun on macOS.

Phase 1 has no checked-in handoff or independent verification report. Phase 2
has a detailed implementation handoff, test record, and completed acceptance
checklist, but no separate independent verification report. Later cumulative
regression results are strong implementation evidence, but they are not a
replacement for the missing phase-specific independent records.

Phase 14 has a completed post-remediation independent verification with a
current `CHANGES REQUIRED` verdict. F-014-05 and F-014-08 remain closed, and
ordinary F-014-09 probe-failure cleanup now removes the created resource. Two
complete native cycles, exact route transitions, named deny counters,
operator-readable three-interface PCAPs, separate sender/receiver evidence,
unrelated canaries, browser History checks, exact successful cleanup, and the
full Linux profile pass. F-014-09 remains open because failed-probe cleanup
deletes an unrelated same-named replacement introduced during that probe.

Migration M0 is a documentation and decision baseline, not a runtime networking
phase. ADR 0016 and ADR 0017 accept the Lima/OVS direction, and
`migration_m0_baseline.md` records a clean macOS quick profile with all 36 tests
passing. The active implementation and verifier contracts now cover M1. No M0
record claims that Lima, configuration version 2, container veth attachment, or
QEMU TAP is implemented.

## Phase status

| Phase | Verification record | Current status | Completion evidence or remaining action |
|---|---|---|---|
| 1 | Missing | **Documentary gap** | Recover historical evidence or perform a retrospective independent verification against the Phase 1 scope. |
| 2 | `phase_2_handoff.md` only | **Implementation complete; independent record missing** | The handoff records passing native, portable, sanitizer, TSan, Compose, and adversarial checks. Create a retrospective independent report rather than relabeling the implementer handoff. |
| 3 | `phase_3_verification.md` | **ACCEPTED** | Final remediation rerun accepted; Linux appendix reports no remaining gate. |
| 4 | `phase_4_verification.md` | **ACCEPTED** | CI, sanitizer, fuzz, analysis, and transport testing accepted; Linux appendix reports no remaining gate. |
| 5 | `phase_5_verification.md` | **ACCEPTED** | Appendix B closes all four original security findings with the full Linux profile. |
| 6 | `phase_6_verification.md` | **ACCEPTED** | Operational telemetry, health, SLO, and dashboard verification accepted; no remaining Linux gate. |
| 7 | `phase_7_verification.md` | **ACCEPTED** | History retention and console findings closed; no remaining Linux gate. |
| 8 | `phase_8_verification.md` | **ACCEPTED** | Authorization and credential-rotation verification accepted; no remaining Linux gate. |
| 9 | `phase_9_verification.md` | **ACCEPTED** | PCAPNG, dissector, extcap, capture security, and Linux verification accepted. |
| 10 | `phase_10_verification.md` | **ACCEPTED** | Release/package contract accepted; report states no remaining Linux acceptance gate. |
| 11 | `phase_11_verification.md` | **ACCEPTED** | Native Linux directed-broadcast and live-capture evidence closes UDP-003/UDP-009. |
| 12 | `phase_12_verification.md` | **ACCEPTED** | External Linux, container TCG/KVM/auto/denial, capture/history, GUI/control, hardening, lifecycle, cleanup, and corrected TShark inspection passed. |
| 13 | `phase_13_verification.md` | **ACCEPTED** | All SDR-001 through SDR-012 rows are implemented; portable default/opt-out and native OVS/SPAN default/opt-out cycles passed; all Phase 13 findings are closed; full Linux plus updated-repository macOS verification passed. |
| 14 | `phase_14_verification.md` | **CHANGES REQUIRED** | Ordinary probe cleanup, completed-entry replacement safety, History navigation, two native cycles, canaries, PCAPs, and full regression pass. Preserve a current-command replacement introduced during a failed identity probe. |

## Review conclusions

1. No checked-in Phase 3 through Phase 12 report has a current acceptance
   blocker.
2. Superseded findings remain valuable historical evidence and should not be
   deleted; they must remain visibly labeled as superseded.
3. Phase 1 and Phase 2 retain documentary gaps in the formal verification chain.
4. Phases 3 through 13 have current accepted independent verification records.
5. Phase 14 remediation passed independent native, canary, PCAP, browser, and
   full regression checks but remains unaccepted because failed identity-probe
   cleanup deletes a same-named replacement of the current resource.
6. Migration M0 accepted the new architecture and froze the portable baseline;
   M1 remains unimplemented until its Lima runtime gates pass.
