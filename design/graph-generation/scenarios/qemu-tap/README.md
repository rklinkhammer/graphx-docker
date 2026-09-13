# S15: qemu-tap

**DESIGN ONLY — every file in this directory is an illustrative non-runtime artifact.**

Input: [graphx.yml](graphx.yml). Shared types: [catalog](../../catalog/README.md). Expected sets: [inventory](expected/inventory.json).

Preserved intent and proposed acceptance: x86_64 TCG boot; guest 10.0.2.15 and peer 10.0.2.2 on VLAN 42; isolated 10.0.2.99 on VLAN 43; TCP/UDP 18001 and 19001; bounded SPAN; QMP evidence distinct from TAP lifecycle.

Current source evidence: [source](../../../../examples/qemu-node/tap/graphx.yaml). Source inspection establishes the current contract, not that v3 generation works. All expected artifacts have status `static-design`; proposed execution has not run. Native platform packaging and generic bindings are proposed in every set.

| Target | Capability cell | Rule and placement/reason | Evidence |
|---|---|---|---|
| lima | supported placement | C-OVS: Docker containers and declared namespace/QEMU/external endpoints on lima; unprivileged container on lima | `static-design`; [expected files](expected/lima/resolved.json) |
| native-linux | supported placement | C-OVS: Docker containers and declared namespace/QEMU/external endpoints on native-linux; unprivileged container on native-linux | `static-design`; [expected files](expected/native-linux/resolved.json) |
| native-macos | unsupported | C-OVS: authored execution cannot be silently moved to another environment | `static-design`; [expected diagnostic](expected/native-macos/diagnostic.json) |
| orbstack | unsupported | C-OVS: authored execution cannot be silently moved to another environment | `static-design`; [expected diagnostic](expected/orbstack/diagnostic.json) |

No target in this case is `not applicable`: each target is a meaningful request for the authored placement and is either supported or explicitly rejected. Native Linux cells that use Docker say so; native macOS does not silently select OrbStack. Lima means execution inside its Linux guest.

| Target | App / platform / auxiliary services | Native app / platform processes | QEMU / capture / handoff processes | Optional scenario / transient adapter / separate build jobs |
|---|---|---|---|---|
| native-linux | 0 / 1 / 0 | 1 / 0 | 1 / 1 / 1 | 0 / 1 / 1 |
| lima | 0 / 1 / 0 | 1 / 0 | 1 / 1 / 1 | 0 / 1 / 1 |

External managed process count is zero. Counts exclude threads and temporary health probes; router namespaces have no daemon. Optional scenario/build jobs do not start with baseline. Lima HTTP access can add one host forwarding process, recorded separately from these guest counts.

Acceptance requirements:

- `planned-portable`: validate the graph with the authoritative C++ compiler; compile twice with shuffled maps; compare all artifact bytes; verify bindings, defaults and diagnostics.
- `planned-docker`: validate Compose with the selected engine, then start and verify application outputs and platform health/history. Use native Linux Docker, OrbStack for supported portable sets, and Lima Docker separately; engine readiness is a prerequisite.
- `planned-privileged`: separately authorized Linux x86_64 and Lima ARM64 runs; verify each declared address, MAC, VLAN, route, policy, mirror/capture and management isolation; inject failure/interruption and compare owned resource state before/after cleanup. OrbStack does not provide this evidence.
- `planned-guest-boot`: x86_64 guest with TCG on Linux x86_64 and Lima ARM64; build the declared recipe, validate digests, boot, complete configuration/credential handshake, exchange declared application traffic, inspect QMP, and stop cleanly. TAP-only success is insufficient. KVM is outside baseline and unverified.

Detailed limits, action formats, credentials and failure/cleanup ownership: [architecture](../../architecture.md). Check commands and honest result status: [verification](../../verification.md).
