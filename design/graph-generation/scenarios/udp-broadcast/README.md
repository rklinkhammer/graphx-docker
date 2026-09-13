# S05: udp-broadcast

**DESIGN ONLY — every file in this directory is an illustrative non-runtime artifact.**

Input: [graphx.yml](graphx.yml). Shared types: [catalog](../../catalog/README.md). Expected sets: [inventory](expected/inventory.json).

Preserved intent and proposed acceptance: 172.31.91.255:47102 on isolated 172.31.91.0/24; publisher interface 172.31.91.10; receiver binds wildcard only for broadcast.

Current source evidence: [source](../../../../examples/udp-broadcast/graphx.yaml). Source inspection establishes the current contract, not that v3 generation works. All expected artifacts have status `static-design`; proposed execution has not run. Native platform packaging and generic bindings are proposed in every set.

| Target | Capability cell | Rule and placement/reason | Evidence |
|---|---|---|---|
| lima | supported placement | C-CONTAINER: Docker containers on lima; unprivileged container on lima | `static-design`; [expected files](expected/lima/resolved.json) |
| native-linux | supported placement | C-CONTAINER: Docker containers on native-linux; unprivileged container on native-linux | `static-design`; [expected files](expected/native-linux/resolved.json) |
| native-macos | unsupported | C-CONTAINER: authored execution cannot be silently moved to another environment | `static-design`; [expected diagnostic](expected/native-macos/diagnostic.json) |
| orbstack | supported placement | C-CONTAINER: Docker containers on orbstack; unprivileged container on orbstack | `static-design`; [expected files](expected/orbstack/resolved.json) |

No target in this case is `not applicable`: each target is a meaningful request for the authored placement and is either supported or explicitly rejected. Native Linux cells that use Docker say so; native macOS does not silently select OrbStack. Lima means execution inside its Linux guest.

| Target | App / platform / auxiliary services | Native app / platform processes | QEMU / capture / handoff processes | Optional scenario / transient adapter / separate build jobs |
|---|---|---|---|---|
| native-linux | 2 / 1 / 0 | 0 / 0 | 0 / 0 / 0 | 0 / 1 / 0 |
| orbstack | 2 / 1 / 0 | 0 / 0 | 0 / 0 / 0 | 0 / 1 / 0 |
| lima | 2 / 1 / 0 | 0 / 0 | 0 / 0 / 0 | 0 / 1 / 0 |

External managed process count is zero. Counts exclude threads and temporary health probes; router namespaces have no daemon. Optional scenario/build jobs do not start with baseline. Lima HTTP access can add one host forwarding process, recorded separately from these guest counts.

Acceptance requirements:

- `planned-portable`: validate the graph with the authoritative C++ compiler; compile twice with shuffled maps; compare all artifact bytes; verify bindings, defaults and diagnostics.
- `planned-docker`: validate Compose with the selected engine, then start and verify application outputs and platform health/history. Use native Linux Docker, OrbStack for supported portable sets, and Lima Docker separately; engine readiness is a prerequisite.

Detailed limits, action formats, credentials and failure/cleanup ownership: [architecture](../../architecture.md). Check commands and honest result status: [verification](../../verification.md).
