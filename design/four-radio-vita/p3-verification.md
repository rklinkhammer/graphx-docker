# P3 verification — jumbo paths and passive recorder

## Current acceptance

**P3 passes: 9/9 cases in the dedicated GraphX Lima guest, Linux ARM64.**
P4 also passes its complete matrix; see [P4 verification](p4-verification.md).
Use the [operator runbook](privileged-verification-runbook.md) for reproduction,
identity checks and cleanup. The user authorized the privileged matrix and the
prior clean recreation without backups. No environment recovery is pending.

Current source base: `3a120c9b2b6f8245ec26b67440ad21c8403cea12`, with the
uncommitted verification corrections. The sole VITA codec/runtime remains
vrt_framework `dbe85d37155145842da60367af1c4beef8801b0c`.

Verified private images: `outputs/verification/p301-fix/images-identity/`.
Each role passed two independent no-cache builds, archive reproducibility,
OCI inventory/SBOM checks and smoke tests. Images were built once for the final
runtime; every fixture created fresh containers. No image was published.

The aggregate machine-readable report is
`outputs/verification/p301-fix/p34-results.json`. It retains the underlying
run results, image digests, harness hashes, selected observations and final
inventory comparison. Full runtime evidence remains guest-local under
`/var/lib/graphx/verification/p301-fix/`.

## Case results

All P3 cases passed in `run-identity/` using its checksum-verified CLI and
`prepared-identity/preparation.json`.

| Case | Check | Result | Seconds |
|---|---|---|---:|
| P3-01 | Baseline application and packet progress | PASS | 14.23 |
| P3-02 | MTU observation and unsafe-path rejection | PASS | 28.07 |
| P3-03 | Maximum UDP payload and mirror bytes | PASS | 14.85 |
| P3-04 | Traffic families and mirror selection | PASS | 17.66 |
| P3-05 | Passive recorder and ingress enforcement | PASS | 17.35 |
| P3-06 | Saturation and bounded storage | PASS | 79.36 |
| P3-07 | Independent diagnostic capture | PASS | 18.79 |
| P3-08 | Identity substitution rejection | PASS | 16.20 |
| P3-09 | Owned cleanup | PASS | 14.65 |
## Observed behavior

The actual OVS path carried 4128-byte IQ and 8836-byte spectrum UDP payloads.
The jumbo and selection fixtures checked probe bytes, mirror delivery and
selection counts. The selection observation included IQ, Context, spectra, ARP
and TCP, with zero observed fragmentation, truncation or outside traffic.

The recorder had zero inheritable, permitted, effective and ambient capabilities
after startup, with seccomp enabled. The independent privileged namespace sender
could not inject into the application path; the owned ingress-drop counter rose.
Receive-descriptor restrictions also passed inside the recorder container.

Saturation held the recorder stopped while upstream and detector progress
continued for 65.17 seconds. Peak recorder RSS was 9,670,656 bytes, below its
128 MiB limit. Socket/log bounds and observable kernel drops passed. The observer
saw no fragmentation or truncation. This is bounded best-effort behavior, not a
lossless capture or sustained-throughput guarantee.

Independent diagnostic capture continued producing fresh packets after recorder
death, within its two-file retention bound. Recorder traffic is discarded; saved
PCAPNG files belong to the separate diagnostic capture lifecycle.

Ledger UUID, ifindex, namespace, image and live-alias substitutions were rejected.
All cases preserved their sentinel. Final infrastructure matched the pre-run
inventory, with no remaining containers or OVS bridges. Intentional retained
history/capture evidence remains under the guest's ownership records.

## Implementation and test boundaries

The recorder's exact `NET_RAW` capability is validated. Its permitted-only
executable capability supports direct non-root startup: open an inactive socket,
drop capabilities before threads, then wait for the identity-owned network
barrier. `no-new-privileges` remains enabled.

Common cleanup quotes diagnostic OVS identities and validates absent or owned
mirrors. Ordinary container-veth ledger records must match their expected
namespace, container, interface names and kind before cleanup can mutate resources.
Negative unit tests cover substituted identities and capability policies.

The MTU fault targets the identity-checked namespace peer because OVS reconciles
its host port to `mtu_request`. Harness counter sampling retains the detector's
periodic summary under its per-spectrum log load; ownership timestamps remain
exact strings. These corrections do not replace production assertions or bypass
ownership guards.

## P3 live case specifications

The executable specification is `tests/test_vita_live.py`, cases P3-01–P3-09.
Detailed selections, resource bounds and recovery commands are in the
[shared runbook](privileged-verification-runbook.md#5-p3-automated-case-matrix).
Run from fresh output roots with reviewed preparation hashes. Runtime artifacts
stay under `/var/lib/graphx`; never use blanket Docker/OVS cleanup.

This evidence qualifies the dedicated Lima ARM64 container path. It does not
qualify a native Linux host, QEMU guest execution, TCG, KVM, physical radios,
physical uplinks or published P5 release images.
