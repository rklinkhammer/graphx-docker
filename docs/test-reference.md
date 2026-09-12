# GraphX verification reference

This document maps verification intent to the maintained test profiles. The
commands, prerequisites, and failure workflow are owned by
[`test-procedure.md`](test-procedure.md); interactive evidence collection is
owned by [`manual-test-procedures.md`](manual-test-procedures.md). Historical
run reports belong under [`archive/`](archive/README.md).

## Test tiers

| Tier | What it proves | Where it runs |
|---|---|---|
| `quick` | Unit behavior, configuration contracts, static plans, documentation links, repository hygiene | macOS or Linux |
| `portable` | Complete unprivileged transports, telemetry, browser, and examples | macOS or Linux |
| `full` | Portable verification plus the selected Docker/Compose engine | macOS OrbStack or Linux Docker |
| `quality` | Formatting and static analysis | Supported developer toolchain |
| `sanitizers` | ASan/UBSan runtime checks | macOS or Linux |
| `fuzz` | Bounded framing and envelope fuzz smoke | Supported Clang toolchain |
| `privileged-linux` | System OVS, veth, TAP, namespaces, routing, policy, capture, faults, and exact cleanup | Native Linux or dedicated Lima guest |
| `release` | Package contents, SBOM, checksums, reproducibility, and release metadata | Clean release checkout |

List exact registered tests from the configured build instead of maintaining a
second hand-written catalog:

```sh
ctest --test-dir build/dev --show-only=json-v1
ctest --test-dir build/dev --print-labels
```

CTest labels are the stable taxonomy:

- `quick` is the everyday feedback loop.
- `stress` contains deliberately repeated timing/concurrency work.
- `privileged` mutates Linux host networking and is opt-in.
- `package` constructs distribution artifacts and belongs to release checks.

## Capability coverage

| Capability | Primary evidence |
|---|---|
| Configuration versions and migration | Config CLI, normalized-contract, and compatibility tests |
| TCP/TLS, UDP, Unix sockets, shared memory | C++ transport tests plus finite example pipelines |
| Telemetry, history, security, and control | Node unit/integration suite and portable system test |
| Browser topology and operations | Web tests plus complete-system Docker acceptance |
| OVS ownership and replacement refusal | Portable plans plus privileged lifecycle tests |
| Container attachment | Container-veth plan/live tests |
| Semantic network profiles and routing | Network-lab plan/live tests |
| QEMU attachment | QEMU TAP plan/live tests; KVM and TCG results remain distinct |
| Ethernet capture and faults | Network-observability plan/live tests |
| Packaging and release | Release-contract, package, and reproducibility workflows |

Each behavior should have one primary automated owner. Independent live audits
may repeat a safety property at a different trust or platform boundary, but
tests should not assert private source text, exact prose, or historical names.

## 6. Native Linux network drivers

These tests are destructive to resources explicitly created by the selected
scenario. Run them only on a dedicated Linux host or in the GraphX Lima guest.
The operator must confirm that declared bridge, namespace, veth, TAP, qdisc,
capture, and state names do not collide with unrelated work.

GraphX records stable object identity and refuses name-only cleanup. A failed
collision or replacement test may intentionally leave the replacement object
for manual inspection; never bypass that refusal with a broad cleanup command.

Use the `privileged-linux` profile for automated coverage and the manual test
guide when an acceptance record needs packet evidence and an explicit host
residue audit.

## OVS mirrors, capture and fault behavior

Application capture and Ethernet capture are separate evidence surfaces.
Application PCAPNG comes from bounded graph envelopes. Ethernet capture comes
from an owned OVS mirror and capture process. Faults are owned qdiscs with
bounded timers. The privileged profile verifies creation, status, expiration,
replacement refusal, recovery, and exact teardown.

## macOS Lima network runtime

OrbStack owns unprivileged macOS Docker acceptance. The dedicated ARM64 Lima
guest owns privileged system-OVS, namespace, nftables, netem, dumpcap, and QEMU
work. Do not forward the guest Docker or privileged sockets to macOS. Results
from this guest are Linux ARM64 evidence, not native macOS or KVM evidence.

### Local Linux verifier container on macOS

Container builds may need an organization root CA. `GRAPHX_CA_CERT` names a
public CA certificate supplied as a BuildKit secret. An optional reviewed
`GRAPHX_CERT_INSTALL_SCRIPT` runs only in the image trust-bootstrap stage; it
must be noninteractive, require no sudo, and contain no private keys, registry
credentials, or npm tokens. Host-side `npm ci` continues to use the operating
system trust store, with the public CA added when configured.

These settings solve trust bootstrap only. They do not turn a macOS container
into evidence for privileged Linux networking.

## Cleanup and failure triage

Start with the first substantive failure and the log path printed by the
profile. Use the scenario's ordinary status and teardown commands. Do not delete
state ledgers or network objects by name to make a test pass: retained state is
often the evidence that prevents deletion of an unrelated replacement.

After privileged work, audit the declared object names and confirm that only
persistent zero-byte lock files remain. After Docker work, use Compose teardown
and confirm no project containers or networks remain.

## Release-candidate verification

Release verification is deliberately separate from quick tests. It validates a
clean source identity, exact archive inventory and modes, bounded extraction,
SPDX SBOM, checksums, trusted tag/commit/platform metadata, and reproducible
candidate construction. Follow [`release-process.md`](release-process.md); do
not use development-only dirty-tree allowances for publication.
