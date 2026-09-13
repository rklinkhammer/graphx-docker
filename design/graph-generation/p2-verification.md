# P2 implementation verification

P2 implements generic resolved node bindings, reusable sample application behavior,
and the local-ready/release interface. The user authorized this phase separately
from the design review. I-01–I-11 remain accepted; no new blocking conflict was found.
Compilation, graph orchestration, automatic credential staging, OVS realization
and guest execution remain gated for their respective phases.

## Implemented contract

- `include/graphx/node_settings.hpp` and `src/node_settings.cpp` validate one
  normalized node object against the existing closed normalized schema and the
  release's embedded catalog type contracts. Identity, type revision, parameters,
  port names, roles, wire schema/encoding, transport, peer limits, resolved endpoint syntax and bounded startup are checked before application resources open.
  `graphx node-settings --node ID --config FILE` exposes the same reader to Python.
- Sample/UDP entry points call reusable `src/sample_application.cpp`. Instance and
  connection identities come from bindings; parameters determine count, interval
  and transform factor. Input wire type, integer parsing, overflow and premature
  stream closure are checked. TCP/UDP senders bind the resolved source address.
  The shared observer lives under `src/`, with actual node/connection trace and
  application-capture identities.
- Applications require `--node`, `--config`, `--release-file` and `--release-token`.
  They report `ready node=ID` after local listeners/resources bind, then wait for
  the exact invocation token before connecting or sending data. Release input is
  bounded, regular and non-symlink; wrong identity, timeout and cancellation fail
  closed. TCP connection/retry and shared-memory initialization accept cancellation;
  established C++ transports close on interruption. P6 owns directory staging,
  ownership checks, readiness collection and atomic release creation.
- SDR services use the C++ reader, bind before release, select source filters and
  endpoints from ports, and identify telemetry with the configured instance ID.
  The control client retains mutual TLS and bounded framing. It rejects malformed
  replies; malformed control expiration values cannot terminate its relay thread.
  `sdrctl.py` requires the controller's node identity and resolved file too.
- Null telemetry credential references disable network telemetry. Non-null refs
  require an explicitly supplied runtime secret; automatic provisioning remains
  P5. GraphX TLS credential mapping stays explicitly gated. SDR retains explicit
  TLS file inputs, with endpoints/server names supplied by bindings. No credential
  values enter normalized JSON or maintained fixtures.

## Verification

All results below are unprivileged native macOS ARM64 results with Node.js 24 on
PATH. No Linux, OrbStack, Lima or guest execution is inferred from these checks.

| Check | Result and evidence |
|---|---|
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick` | **PASS**, 31/31 CTests; [log](../../outputs/verification/20260913T152055Z-quick.log) |
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality` | **PASS**, LLVM 21 formatting, clang-tidy and cppcheck; [log](../../outputs/verification/20260913T152304Z-quality.log) |
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable` | **PASS**, 31/31 CTests, 32 launcher gates, 92 telemetry tests, 17 web tests and build; [log](../../outputs/verification/20260913T152311Z-portable.log) |
| Focused CTest run shown below | **PASS**, 4/4, including installed consumer/archive contract verification |
| `git diff --check` | **PASS** |
| ShellCheck | Not applicable: no shell scripts changed |

The focused verification command was:

```sh
ctest --test-dir build/dev --output-on-failure -R 'graphx-(documentation-consistency|node-bindings|sdr-node-portable|package)$'
```

The final portable run includes the final C++ binaries and the application capture
contract test. The capture check confirms resolved settings and renamed identities
are authoritative even when legacy capture environment variables are present.

The focused native fixture test runs S01 TCP, S02 shared memory, S03 UDP unicast,
S04 multicast loopback with TTL 0, and S06 application capture. It runs the authored
T01 east/west topology with different factors and two additional independent
renamed pipelines concurrently. It verifies message count/value, actual identities,
no cross-pipeline identifiers and no application traffic before release.

The same test runs renamed raw SDR services with temporary test certificates,
checks local readiness without sample/control probes, receives results and tunes
through the authenticated controller binding. Existing SDR tests retain malformed
framing, rejected controls, unauthorized/untrusted/expired client certificates,
source filtering and protocol bounds. Node negatives cover missing/wrong identity,
revision, schema, encoding, roles, endpoints, cardinality, parameters, arguments,
wrong/symlink release files, timeout and interrupted readiness/TCP retry.

All application fixtures, captures, certificates, release files and logs use
private temporary directories. TCP/UDP use loopback and ephemeral ports; shared
memory uses test-specific names and the existing listener ownership lifecycle.
Tests terminate and reap their child processes in cleanup. No Docker workloads,
OVS resources, namespaces, veth/TAP, nftables, Lima VM operations, TCG or KVM guest
boots are part of this phase's verification. Native Linux, OrbStack and Lima
execution evidence are not claimed from native macOS tests.

The expected compiler artifacts under `design/graph-generation` remain design
fixtures. These tests extract node objects from authoritative normalization and
adapt them into isolated native application fixtures; they do not implement P3
compilation or P6 ownership/orchestration. No production graph launcher is enabled.
