# Migration M4 implementation handoff

**Status:** Remediated after independent verification; re-verification required  
**Date:** 2026-09-09

## Delivered boundary

M4 extends the M3 ownership lifecycle from system-datapath OVS bridges to
managed-container veth endpoints. Version-2 deployments declare a Compose
project; each realized endpoint declares host/target interface names, switch,
address, optional MAC, bounded MTU, and optional routes.

Container selection uses Compose project and service labels and verifies the
image, full container ID, running PID, and namespace inode before state
publication. The ledger records endpoint intent, both ifindices, OVS Port and
Interface UUIDs, container ID, namespace inode, and intrinsic ownership markers.
Status detects restart/replacement. Destroy removes endpoints before bridges and
does not enter a replacement namespace.

## Compatibility

Version-1 runtime behavior is unchanged. Deterministic migration now emits a
Compose project, deterministic interface names, explicit switch references, and
a synthetic system OVS bridge when no legacy switch can be inferred. It never
invents a missing address; such output validates for review but M4 create refuses
realization until an address is declared.

## Evidence completed locally

- Development build succeeds on macOS.
- Configuration C++ tests, configuration-v2 migration tests, M3 ownership
  regression, and the new M4 portable contract test pass.
- All 40 development tests and all 40 enabled sanitizer tests pass; the quality
  build passes and all five M0 version-1 dry-run fingerprints remain exact.
- A real ARM64 Lima run passed the M4 create/status/restart-detection/destroy/
  reattach/status/destroy regression, copied-marker Port and Interface
  replacement refusals, host-ifindex replacement refusal, rollback, hard-crash
  recovery, and the retained M3 live regression. The targeted containers, veth,
  OVS bridges, and disposable test image were removed afterward.
- Independent verification must repeat and broaden the Linux/Lima replacement,
  collision, and interruption matrix before M4 acceptance.

## Deferred boundary

Linux namespace veth, QEMU TAP, semantic-profile flows, mirrors, general router
policy, faults, capture realization, and existing-laboratory migration remain M5
and later work.

## Verification remediation

The failed verification report is preserved at SHA-256
`0954cc10888a542b0b38941a16f989ff5dca2aab6cce6524684f0e1a6730f0d0`.
Remediation now compares the expected OVS endpoint name with the recorded Port
and Interface UUIDs during complete-set preflight and again immediately before
endpoint deletion. Bridge deletion also records its implicit internal Port UUID
and atomically requires that exact one-Port final set.
The privileged M4 regression replaces Port and Interface records with same-named,
copied-marker resources, and replaces the host link with a same-named link and
copied alias. It proves destroy refuses while preserving the replacement,
bridge, and ledger.
