# S03: udp-unicast

**DESIGN ONLY — every file in this directory is an illustrative non-runtime artifact.**

Input: [graphx.yml](graphx.yml). Shared types: [catalog](../../catalog/README.md). Expected sets: [inventory](expected/inventory.json).

Preserved intent and proposed acceptance: 127.0.0.1:47101; u32be; datagrams 1400; buffers 65536.

Current source evidence: [source](../../../../examples/udp-unicast/graphx.yml). Source inspection establishes the current contract, not that v3 generation works. All expected artifacts have status `static-design`; proposed execution has not run. Native platform packaging and generic bindings are proposed in every set.

| Target | Capability cell | Rule and placement/reason | Evidence |
|---|---|---|---|
| lima | supported placement | C-NATIVE: native processes on lima; native Node.js process on lima | `static-design`; [expected files](expected/lima/resolved.json) |
| native-linux | supported placement | C-NATIVE: native processes on native-linux; native Node.js process on native-linux | `static-design`; [expected files](expected/native-linux/resolved.json) |
| native-macos | supported placement | C-NATIVE: native processes on native-macos; native Node.js process on native-macos | `static-design`; [expected files](expected/native-macos/resolved.json) |
| orbstack | unsupported | C-NATIVE: authored execution cannot be silently moved to another environment | `static-design`; [expected diagnostic](expected/orbstack/diagnostic.json) |

No target in this case is `not applicable`: each target is a meaningful request for the authored placement and is either supported or explicitly rejected. Native Linux cells that use Docker say so; native macOS does not silently select OrbStack. Lima means execution inside its Linux guest.

| Target | App / platform / auxiliary services | Native app / platform processes | QEMU / capture / handoff processes | Optional scenario / transient adapter / separate build jobs |
|---|---|---|---|---|
| native-linux | 0 / 0 / 0 | 2 / 1 | 0 / 0 / 0 | 0 / 1 / 0 |
| native-macos | 0 / 0 / 0 | 2 / 1 | 0 / 0 / 0 | 0 / 1 / 0 |
| lima | 0 / 0 / 0 | 2 / 1 | 0 / 0 / 0 | 0 / 1 / 0 |

External managed process count is zero. Counts exclude threads and temporary health probes; router namespaces have no daemon. Optional scenario/build jobs do not start with baseline. Lima HTTP access can add one host forwarding process, recorded separately from these guest counts.

Acceptance requirements:

- `planned-portable`: validate the graph with the authoritative C++ compiler; compile twice with shuffled maps; compare all artifact bytes; verify bindings, defaults and diagnostics.
- `planned-portable`: run on native Linux x86_64 and macOS ARM64, plus a separately reported guest-native Lima run; verify the precise transport semantics above, bounded shutdown, signed telemetry and persistent bounded history. No Docker is required for these sets.

Detailed limits, action formats, credentials and failure/cleanup ownership: [architecture](../../architecture.md). Check commands and honest result status: [verification](../../verification.md).
