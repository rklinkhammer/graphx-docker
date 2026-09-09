# Migration M0 baseline

**Recorded:** 2026-09-08 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Branch:** `main`  
**Commit:** `2280ba3387f036f08acc79e9ac54613d2680d807` (`2280ba3 ssl permissions`)  
**Product version:** GraphX 1.1.0

## Purpose

This is the frozen pre-migration comparison point for the Lima/OVS migration.
It records portable behavior only. It is not evidence for native OVS, veth,
namespace, nftables, netem, TAP, KVM, or cleanup behavior.

The worktree was clean before M0 documentation was added.

## Platform

- macOS 26.6.2, build 25G83
- Darwin 25.6.0, arm64
- Apple Clang 21.0.0
- CMake 4.4.3

## Portable baseline

`scripts/verify.sh quick` configured a clean development build and passed all
36 CTest entries in 19.52 seconds. The complete quick profile finished in 26
seconds. Covered suites included core runtime, configuration, TLS, capture,
extcap/Wireshark, projections, documentation, packaging, current network
example validation, UDP, QEMU static checks, SDR portable checks, and the
static-route-policy portable contract.

The generated verification log was:

```text
outputs/verification/20260909T000027Z-quick.log
```

`graphx project graphx.yaml --check --output-dir config` reported all four
checked-in projections current.

The following version-1 configurations validated successfully:

- `examples/macvlan/graphx.yaml`
- `examples/ipvlan-l2/graphx.yaml`
- `examples/ipvlan-l3/graphx.yaml`
- `examples/mixed-network/graphx.yaml`
- `examples/qemu-node/external/graphx.yaml`
- `examples/qemu-node/container/graphx.yaml`
- `examples/sdr-node/external/graphx.yaml`
- `examples/static-route-policy/graphx.yaml`

## Infrastructure-plan fingerprints

These SHA-256 values cover the exact stdout of `graphx infra create CONFIG
--dry-run` at the baseline commit:

| Configuration | SHA-256 |
|---|---|
| `examples/macvlan/graphx.yaml` | `40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d` |
| `examples/ipvlan-l2/graphx.yaml` | `da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37` |
| `examples/ipvlan-l3/graphx.yaml` | `598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b` |
| `examples/mixed-network/graphx.yaml` | `ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc` |
| `examples/static-route-policy/graphx.yaml` | `33c53b043727d2404f1dcf4a8186dd1ce0b92536b49ead19f20416a166a355db` |

The fingerprints are drift indicators, not semantic acceptance evidence.

## Current realization boundary

At this baseline:

- configuration version 1 selects Docker `bridge`, `macvlan`, or `ipvlan`
  drivers;
- the infrastructure planner creates external Docker networks;
- OVS is an adjacent explicitly modeled switch/router/capture facility rather
  than the only backend;
- Compose and example scripts complete container endpoint attachment;
- the macOS mixed-network profile uses Docker bridge networks and a privileged
  userspace-OVS container;
- QEMU uses user/slirp networking and its TAP/OVS mode is deferred;
- generic infrastructure state is not persisted or reconciled, although the
  Phase 14 laboratory contains stronger run-local ownership checks.

## Comparison rule

Every later migration phase must compare affected portable checks with this
baseline and add platform-appropriate runtime evidence. A changed dry-run hash
is expected when its phase intentionally changes realization, but the handoff
must explain the change rather than updating this record.

