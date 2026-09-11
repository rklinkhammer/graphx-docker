# Migration M8 remediation handoff

**Recorded:** 2026-09-10 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Source report:** `migration_m8_verification.md`  
**Implementation state:** uncommitted and awaiting independent re-verification

## Outcome

M8-V01, M8-V02, and M8-V03 are remediated. The supported Linux acceptance
runner uses only canonical OVS lab entry points and explicitly selects its Linux
GraphX binary. Active operator documentation no longer names retired launchers.
Lima now installs and verifies Node.js 24 with `node:sqlite` from the same
digest-pinned official multi-architecture image as the Linux verifier.

The canonical wrapper executable bits were also restored after direct execution
exposed that the M8 working tree had changed them to non-executable. Regression
coverage now checks their existence, executable mode, and common OVS lifecycle.

This handoff does not alter the failed historical verdict in
`migration_m8_verification.md`; independent re-verification owns the new verdict.

## Finding closure

| Finding | Remediation | Regression | State |
| --- | --- | --- | --- |
| M8-V01: native-Linux runner selected deleted paths | Removed the mixed-network special case and imperative fault calls; all four labs execute `up.sh`, `status.sh`, and `down.sh`. The runner exports `GRAPHX_BIN` from its Linux build directory. | M8 portable closure scans active scripts/docs for every retired launcher name and requires the explicit Linux binary selection. | Remediated |
| M8-V02: active docs named deleted launchers | Updated the graphical guide and test procedure/reference to use canonical `scripts/down.sh` and only explicitly supported `down-native-linux.sh` helpers. | M8 portable closure rejects retired launcher vocabulary outside historical ADRs/runbooks. | Remediated |
| M8-V03: Lima installed Node.js 18 | Removed Ubuntu `nodejs`/`npm` packages. Provisioning copies `/usr/local` from the pinned Node.js 24 image, validates the major version and `node:sqlite`, records image/version evidence, and adds readiness/verification probes. | Lima static tests require the shared image digest, reject distro Node packages, and require version plus SQLite probes in provisioning, readiness, start, and verification. | Remediated |

## Additional closure

- Canonical lab wrappers are executable and tested as commands, not merely read
  as files.
- Persistent zero-byte mode-0600 per-graph lock files are documented as
  serialization metadata, distinct from removed ownership YAML ledgers and live
  runtime residue.
- The old verification VM was updated in place with the exact pinned runtime
  recipe solely for remediation testing. Because the source configuration
  digest changed, normal lifecycle identity checks correctly require deliberate
  recreation before fresh independent release acceptance.

## Validation evidence

- Host quick gate: PASS, 45/45 tests,
  `outputs/verification/20260911T010847Z-quick.log`.
- Host quality gate: PASS, 49-file formatting plus clang-tidy/cppcheck,
  `outputs/verification/20260911T010929Z-quality.log`.
- Focused M8 portable, Lima static, documentation consistency, shell syntax,
  and `git diff --check`: PASS.
- Lima runtime: Node.js `v24.20.0`, npm `11.19.0`, and `node:sqlite` available.
- Documented `scripts/test-features.sh linux-network`: PASS as the Lima login
  user, including C++23 45/45, C++20 45/45, telemetry 77/77, web 17/17 and
  production build, live UDP packet capture, and all four canonical OVS labs.
- Direct canonical `macvlan`, `ipvlan-l2`, `ipvlan-l3`, and `mixed-network`
  executable launcher sequence: PASS.
- Expanded M8 live closure: PASS twice after remediation; each invocation ran
  two complete lifecycle cycles for all four canonical labs.
- Lima prerequisite verifier with the remediated runtime checks: PASS, evidence
  `/var/lib/graphx/m1/evidence/20260911T011302Z-1f7c838a`.
- Final runtime residue: no OVS bridges, namespaces, veth/TAP devices, qdiscs,
  containers, Docker macvlan/ipvlan networks, capture/QEMU processes, or YAML
  ownership ledgers. Only the documented inactive per-graph lock files remain.

Native Linux ARM64, native Linux x86_64 TCG, and native Linux x86_64 KVM were
not executed and are not claimed.
