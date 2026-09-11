# Migration M6 independent verification

**Verdict:** Rejected pending remediation and re-verification  
**Candidate:** `c12a9a4` (`M5/M6 Baseline`)  
**Date:** 2026-09-10  
**Verifier scope:** M6-001 through M6-010, derived from `prompt/verifier.md`,
`prompt/implement.md`, ADR 0017, and the accepted M5 report. The implementation
handoff was treated as an untrusted claim. No product fixes were made.

## Blocking findings

### M6-V1 — The checked-in Lima environment is not the environment under test

**Severity:** P1 / acceptance blocker  
**Requirements:** M6-001, M6-004, M6-005

The repository's authoritative host verifier refuses the running `graphx` VM:

```text
Refusing stale GraphX instance configuration
(d2109a9bc8cd39737964bd53091a0a9ec8af045ee674491801dc5794254118e9);
expected 436c6c2c90a9fba7effdebfde028e9b5be3a8b4c91f4f18a8ae01c11bd862d3c.
```

This is expected after the M6 provisioning change that adds the dedicated
`graphx-qemu` account, but it means a fresh instance created from the candidate
was not verified. The existing VM is ARM64/VZ and had the required 65532 account,
so it was useful for independent runtime testing, but it cannot certify the
checked-in Lima definition. Verification did not delete and recreate the user's
VM because this was a verification-only task and that operation is destructive.

**Required remediation:** deliberately remove and recreate `graphx` using the
documented Lima lifecycle, then rerun `infrastructure/lima/verify.sh` and the M6
guest profile from that fresh instance.

### M6-V2 — Fresh Lima provisioning omits the Docker Buildx dependency required by the project

**Severity:** P1 / reproducibility blocker  
**Requirements:** M6-001, M6-005

`infrastructure/lima/provision.sh` installs `docker.io` and Compose but not the
available Ubuntu `docker-buildx` package. Both the root image and the QEMU builder
use BuildKit-only `RUN --mount=type=secret` instructions; the QEMU build launcher
invokes `docker build` directly.

Observed in the VM:

```text
$ docker buildx version
docker: unknown command: docker buildx

$ docker build -t graphx-demo:latest .
the --mount option requires BuildKit

$ DOCKER_BUILDKIT=1 docker build -t graphx-demo:latest .
ERROR: BuildKit is enabled but the buildx component is missing or broken
```

`apt-cache` confirms `docker-buildx` is available for Ubuntu Noble ARM64. This
prevents a clean candidate from rebuilding the project or the M6 guest artifacts
inside the documented Lima environment. Existing guest artifacts allowed the
runtime checks below, but do not satisfy fresh-environment reproducibility.

**Required remediation:** provision and probe `docker-buildx`, then prove both
the root project image and `examples/qemu-node/scripts/build.sh` can start their
BuildKit builds in a newly created VM.

## Requirement matrix

| ID | Result | Independent evidence |
|---|---|---|
| M6-001 | **Blocked** | Fresh host `quick`, `quality`, `sanitizers`, and `portable` profiles passed. C++20 and C++23 each passed 42/42 tests; telemetry passed 77/77 and console 16/16. M0/M5 fingerprints, projections, documentation, and frozen M2-M5 portable contracts passed. Fresh Linux build passed all non-privileged tests; M3 and M5 live tests passed, and M4 passed with a disposable verifier image. Fresh Lima identity and standard image builds are blocked by M6-V1/V2. |
| M6-002 | **Pass** | The focused M6 portable suite passed. It validates migration and rejects missing, root, and misplaced TAP UID/GID plus forbidden QEMU TAP peer/routes. The checked-in profile validated as v2 with two nodes, four edges, and one network. |
| M6-003 | **Pass** | Dry-run contains `datapath_type=system`, direct `ip tuntap`, namespace veths, OVS ports, and SPAN. It contains no Docker data network, macvlan, ipvlan, slirp, or userspace OVS datapath. Live inspection matched the plan. |
| M6-004 | **Blocked** | In the existing ARM64/VZ Lima VM, a fresh native build succeeded. The privileged M6 regression was invoked twice (four clean lifecycle cycles total), including rollback and crash recovery. Additional independent adversarial checks proved complete preservation of same-name replacement TAP and OVS Interface objects. TAP UID/GID, ifindex/alias, OVS UUIDs/markers, VLANs, and M6 ledger state were inspected. The VM itself is stale per the authoritative fingerprint check (M6-V1). |
| M6-005 | **Blocked** | The actual x86_64 guest booted under TCG; QMP reported TCG with KVM absent. QEMU ran with UID/GID 65532, zero effective capabilities, exactly one `/dev/net/tun` descriptor, and only `-netdev tap,...ifname=gxqtap0` (no user/slirp netdev). Fresh artifact production is blocked by M6-V2. |
| M6-006 | **Pass** | Guest TCP and UDP echo passed. OVS FDB learned `02:00:00:00:02:15`. TAP and peer were VLAN 42, the isolation peer was VLAN 43 and could not reach the guest. SPAN frames showed VLAN-42 broadcast to `10.0.2.255` and multicast to `239.1.2.3`. |
| M6-007 | **Pass** | Raw SPAN PCAP grew and decoded correctly. PCAPNG contained guest/peer TCP and UDP frames. SQLite history contained 201 independently inspected records across TCP/UDP and the four expected edge identities. Evidence remained bounded on `/var/lib/graphx/qemu/m6`; final retained files totaled less than 101 KiB. |
| M6-008 | **Pass** | QMP confirmed `paused`; an independent guest probe failed while paused. QMP then confirmed `running`, readiness returned to TCP/UDP true, and the full network verification passed after resume. |
| M6-009 | **Pass** | Netem applied as `delay 50ms 5ms loss 1%`; traffic still passed, and removal returned the peer to `noqueue`. Two actual-profile up/down cycles completed. Final audit found no M6 bridge, TAP, veth, namespace, netem qdisc, ledger, or QEMU process. Verifier-created build/image artifacts were also removed. |
| M6-010 | **Pass** | External and container user-mode networking remain documented compatibility paths. The TAP profile explicitly reports x86_64 TCG/KVM absent on Apple Silicon. Declarative capture/fault ownership is explicitly deferred to M7. |

## Test evidence

- `scripts/verify.sh quick`: pass, 42/42 enabled tests.
- `scripts/verify.sh quality`: pass, LLVM 21 formatting/static analysis.
- `scripts/verify.sh sanitizers`: pass, 42/42 enabled tests plus sanitizer
  coverage (platform-safe UBSan on macOS 26; Linux remains the ASan+UBSan path).
- `scripts/verify.sh portable`: pass, C++23 42/42, C++20 42/42, telemetry
  77/77, console 16/16, examples and control-plane checks.
- `tests/test_m5_lab_migration.py`: pass, frozen legacy fingerprints retained.
- `tests/test_m6_qemu_tap.py`: pass.
- Fresh ARM64 Linux configure/build: pass.
- Privileged live regressions: M3 pass, M5 pass, M6 pass twice. M4 initially
  exposed M6-V2; with a disposable Debian image containing `iproute2`, the M4
  test itself passed. The disposable image was removed.
- Actual M6 guest profile: pass twice, with QMP, traffic, VLAN, capture/history,
  pause/resume, netem, and teardown inspection.
- `git diff --check`: pass before writing this report.

Host logs are under `outputs/verification/`:

- `20260910T153059Z-quick.log`
- `20260910T153157Z-quality.log`
- `20260910T153258Z-sanitizers.log`
- `20260910T154517Z-portable.log`

## Re-verification entry criteria

1. Add and verify the Buildx dependency in Lima provisioning.
2. Recreate the `graphx` Lima instance so its recorded digest matches the
   candidate.
3. Run the authoritative Lima verifier successfully.
4. Rebuild the QEMU guest artifacts from the clean VM.
5. Repeat the privileged M6 lifecycle twice and the actual guest profile once,
   followed by the exact residue audit.

M7 must not begin until both blockers are closed and M6 is accepted.
