# Migration M6 independent re-verification

**Verdict:** Accepted  
**Candidate:** `c12a9a4` plus the uncommitted M6 remediation working tree  
**Candidate Lima digest:** `b3bf15d5adff4f887254a514130fd3c027fb1bbe89369a108534d99471a70509`  
**Date:** 2026-09-10  
**Scope:** M6-001 through M6-010 from `prompt/implement.md`,
`prompt/verifier.md`, ADR 0017, the accepted M5 boundary, and the original M6
verification report. The remediation handoff was treated as an untrusted claim.
No product fixes were made during re-verification.

## Outcome

The two original acceptance blockers are closed. The running `graphx` Lima VM
has the exact candidate configuration digest, is ARM64 under Apple VZ, and
passes the authoritative Linux verifier. Ubuntu's Buildx plugin is installed,
the ordinary Lima login identity belongs to the rootful Docker group, and both
the project image and QEMU Buildroot builder complete BuildKit builds without
`sudo`.

All retained host gates, privileged lifecycle tests, the actual QEMU guest
profile, adversarial replacement checks, and final residue audits passed. M6 is
accepted and no longer blocks M7.

## Original finding closure

| Finding | Re-verification evidence | Result |
|---|---|---|
| M6-V1: stale Lima environment | `graphx_m1_assert_identity` reported `Running`; `/etc/graphx-m1-config.sha256` and the candidate both contained `b3bf15d5adff4f887254a514130fd3c027fb1bbe89369a108534d99471a70509`. `limactl` reported `graphx Running aarch64 vz`. The authoritative verifier passed and retained `/var/lib/graphx/m1/evidence/20260910T212041Z-7f4c3b94`. | Closed |
| M6-V2: Buildx missing | The login user was `rklinkhammer`, with supplementary group `docker`; `docker info` worked without `sudo`, used `/var/lib/docker` with `overlayfs`, and Buildx reported 0.30.1. A fresh ordinary-user project image build and an ordinary-user QEMU builder/artifact build both completed BuildKit-only secret-mount stages. | Closed |

## Requirement matrix

| ID | Result | Independent evidence |
|---|---|---|
| M6-001 | **Pass** | Fresh `quick`, `quality`, `sanitizers`, and `portable` profiles passed. C++23 and C++20 each passed 42/42 tests; telemetry passed 77/77 and console passed 16/16. Projection, documentation, package, frozen-version, M2-M5, and focused M5/M6 gates passed. The exact Lima candidate passed its authoritative verifier, fresh project-image build, and QEMU artifact build. |
| M6-002 | **Pass** | The v2 TAP profile validated with two nodes, four edges, and one network. Focused portable tests rejected missing UID/GID, UID 0, misplaced TAP ownership, and forbidden QEMU TAP peer data. Migration/frozen-version gates passed. |
| M6-003 | **Pass** | Dry-run showed a system OVS bridge, direct persistent TAP creation for UID/GID 65532, namespace veths, access VLANs, and an OVS SPAN mirror. It contained no Docker data network, macvlan, ipvlan, slirp, or userspace OVS fallback. Live state matched the plan; Docker retained only its default `bridge`, `host`, and `none` networks. |
| M6-004 | **Pass** | M3 and M4 live tests passed, M5 passed twice, and two M6 test invocations each passed two lifecycle cycles. These covered identity-bearing TAP/OVS state, ledger phase, injected failure rollback, hard-crash recovery, replacement refusal, and exact cleanup. Independent adversarial tests additionally replaced the same-name TAP by ifindex and OVS Interface by UUID; GraphX refused cleanup and preserved the replacements and ledger. |
| M6-005 | **Pass** | The actual x86_64 guest booted under TCG in ARM64 Lima. QMP reported TCG with KVM absent. QEMU ran as UID/GID 65532 with `CapEff=0`, one `/dev/net/tun` descriptor, and only `-netdev tap,...ifname=gxqtap0`; no user/slirp netdev was present. The independently rebuilt artifacts matched their manifest. |
| M6-006 | **Pass** | Guest TCP and UDP echo passed. OVS learned guest MAC `02:00:00:00:02:15`. TAP and peer ports were access VLAN 42, the isolation port was VLAN 43 and could not reach the guest, and packet inspection found broadcast and multicast traffic, including `239.1.2.3`. |
| M6-007 | **Pass** | SPAN PCAP grew during traffic. Retained PCAPNG contained 206 packets and SQLite history contained 44 records. Evidence lived on `/var/lib/graphx/qemu/m6`; PCAPNG and history were owned by 65532:65532. Observer limits enforce 100,000 packets, 64 MiB capture/database defaults, 50,000 history records, retention time, and bounded query pages. |
| M6-008 | **Pass** | QMP confirmed `paused`; TCP/UDP state became unavailable and an independent guest probe failed. QMP then confirmed `running`; TCP/UDP readiness returned and the complete network verification passed after resume. |
| M6-009 | **Pass** | Netem was observed as `delay 50ms 5ms loss 1%`, traffic still passed, and removal restored `noqueue`. Teardown removed the bridge, TAP, veths, namespace, qdisc, ledger, QMP socket, PID files, and all identity-checked processes. Final audit after artifact rebuilding remained clean. Verifier-created Docker images were removed. |
| M6-010 | **Pass** | External and container `qemu-usernet` profiles retain explicit slirp compatibility meaning. The TAP profile documents and reports TCG rather than KVM on Apple Silicon. Declarative capture and fault ownership remains explicitly deferred to M7. |

## Re-verification evidence

- Host verification logs:
  - `outputs/verification/20260910T211750Z-quick.log`
  - `outputs/verification/20260910T211819Z-quality.log`
  - `outputs/verification/20260910T211833Z-sanitizers.log`
  - `outputs/verification/20260910T211901Z-portable.log`
- Authoritative Lima evidence:
  `/var/lib/graphx/m1/evidence/20260910T212041Z-7f4c3b94`.
- Fresh ordinary-user project image:
  `sha256:1a3866ea639bd823fb27b8d86b484769dfcc1e744d61d4f4b745e2d28180a792`,
  configured as UID/GID 65532:65532.
- Independently rebuilt Buildroot 2025.02.17 artifacts:
  - `bzImage`: 6,329,344 bytes,
    SHA-256 `61bdc74cd32ff9bd801ec42cd07d45abbaea9fdd303ca8e9e9fff74697dc99de`;
  - `rootfs.cpio.gz`: 1,003,664 bytes,
    SHA-256 `7baa4fb05d012b43330fa0c48285ba0b125133a8155e8ec07fe453d0bb354fa3`.

`git diff --check` passed before this report was written. The original
`migration_m6_verification.md` rejection remains unchanged as the audit record;
this report supersedes its verdict for the remediated candidate.
