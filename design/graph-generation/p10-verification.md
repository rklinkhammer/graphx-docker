# P10 removal and release verification

P10's removal work and available platform acceptance are complete. The release
qualification remains open for native Linux x86_64 and clean, independently
repeated release builds. No release was published and no VM was provisioned.

## Current implementation

- Source Compose files and disabled launchers are removed. Retained example
  wrappers consume immutable compiled output through the common runner.
- The obsolete v2 live fixtures, standalone packet observer and duplicate schema
  snapshots are removed. Current C++/normalized schemas and compiler tests are
  authoritative; illustrative design outputs remain explicitly non-runtime.
- The privileged CTest invokes the compiled OVS, scenario and guest harnesses,
  sequentially, with explicit authorization and verified artifact roots. Lima
  verification propagates those prerequisites without changing VM configuration.
- Test harnesses accept verified installed executables and support both classic
  Docker configuration IDs and containerd OCI manifest IDs for verified images.
- Missing telemetry and zero-application graphs no longer display fabricated sample
  nodes, edges or network paths. Focused web tests cover both empty states.

## Executed verification

| Check | Result and evidence |
|---|---|
| Full macOS verification | Passed host/Linux-container quality, supported sanitizer suites, bounded fuzzing, portable checks and the OrbStack matrix; [full log](../../outputs/verification/20260914T005708Z-full.log) |
| Final portable regression | Passed after the bounded live native fixture change; [log](../../outputs/verification/20260914T011525Z-portable.log) |
| Authoritative input/compiler matrix | Passed 63 supported target sets, 15 negative inputs and 33 unsupported-target diagnostics as part of full verification; includes repeat/shuffled/relocated compilation tests |
| OrbStack ARM64 portable matrix | All 11 cases passed, with the separately owned sentinel preserved; [results](../../outputs/p10/acceptance/orbstack-portable-results.json) |
| Linux ARM64 Docker in Lima | All 11 portable cases passed with the sentinel preserved; [results](../../outputs/p10/acceptance/linux-portable-results.json) |
| Lima OVS and scenarios | All six OVS cases and S11/S12/S14 actions passed; per-case results below include cleanup/inventory checks |
| Actual guest execution | S15 and T03 passed with x86_64 guests under TCG in ARM64 Lima; this is guest boot/application evidence, not only TAP lifecycle evidence |
| Safari, native macOS ARM64 | Current source web build passed authenticated topology, both edge inspectors, Fit View, pause/resume acknowledgements and SQLite history/control/SLO observations; [results](../../outputs/p10/browser-results.json), [cleanup](../../outputs/p10/browser-cleanup.log) |
| Schema/design inventory | Passed using authoritative schemas, retained illustrative artifact hashes and link checks; [result](../../outputs/p10/design-check.json) |
| ShellCheck | Passed the exact invocation below |

```sh
shellcheck -x -P .:scripts:infrastructure/lima \
  scripts/test-linux-network-features.sh scripts/test-runtime-features.sh \
  examples/sdr-node/external/scripts/demo.sh infrastructure/lima/verify.sh
```

## Case coverage

These are execution results in the named environments, not a claim that every
supported architecture/target combination has run. Input/compile target coverage
is separate. S14 refers exclusively to the explicitly selected laboratory.

| Case | Authored example | Actual execution evidence |
|---|---|---|
| S01 | sample-pipeline | OrbStack and Linux ARM64 Docker matrices |
| S02 | shared-memory | Native macOS portable lifecycle and rotation |
| S03 | udp-unicast | Native macOS lifecycle |
| S04 | udp-multicast | Native macOS lifecycle |
| S05 | udp-broadcast | OrbStack and Linux ARM64 Docker matrices |
| S06 | capture | Native macOS lifecycle and application PCAPNG |
| S07 | macvlan | [Lima OVS](../../outputs/p10/acceptance/macvlan/results.json) |
| S08 | ipvlan-l2 | [Lima OVS](../../outputs/p10/acceptance/ipvlan-l2/results.json) |
| S09 | ipvlan-l3 | [Lima OVS](../../outputs/p10/acceptance/ipvlan-l3/results.json) |
| S10 | mixed-network | [Lima OVS](../../outputs/p10/acceptance/mixed-network/results.json) |
| S11 | network-observability | [Lima OVS](../../outputs/p10/acceptance/network-observability/results.json), [timed fault](../../outputs/p10/acceptance/S11/results.json) |
| S12 | static-route-policy | [Lima OVS](../../outputs/p10/acceptance/static-route-policy/results.json), [route actions](../../outputs/p10/acceptance/S12/results.json) |
| S13 | sdr-node/simulated | OrbStack and Linux ARM64 Docker matrices |
| S14 | sdr-node/external, laboratory selection | [Lima simulator, test trust, control and cleanup](../../outputs/p10/acceptance/S14/results.json) |
| S15 | qemu-node/tap | [Lima actual x86_64 TCG, traffic, VLAN, capture and QMP](../../outputs/p10/acceptance/S15/results.json) |
| V01 | history | OrbStack and Linux ARM64 Docker matrices |
| V02 | observability | OrbStack and Linux ARM64 Docker matrices |
| V03 | control | OrbStack and Linux ARM64 Docker matrices; native Safari check |
| V04 | credential-rotation | Both Docker matrices; [OrbStack overlap/expiry actions](../../outputs/p10/credential-rotation/results.json) |
| V05 | secure-otlp | OrbStack and Linux ARM64 Docker matrices |
| V06 | otlp-mtls | OrbStack and Linux ARM64 Docker matrices |
| T01 | renamed-multi-source | OrbStack and Linux ARM64 Docker matrices |
| T02 | multi-radio | OrbStack and Linux ARM64 Docker matrices |
| T03 | mixed-container-qemu-sdr | [Lima actual x86_64 TCG SDR guest](../../outputs/p10/acceptance/T03/results.json) |

The native S03/S04/S06 run logs and retained runtime evidence are under
`outputs/p10/native-udp-unicast`, `native-udp-multicast` and `native-capture`.
The browser test used a disposable local native fixture and current web assets;
it does not qualify the UI bytes in a newly published image.

## Reproduction and ownership

The privileged suite ran with:

```sh
limactl shell --workdir /workspace/graphx-docker graphx -- env \
  GRAPHX_ALLOW_PRIVILEGED_TESTS=1 GRAPHX_TEST_TARGET=lima \
  GRAPHX_IMAGE_RELEASE=/var/lib/graphx/runtime/p7-acceptance/images-store \
  GRAPHX_TEST_RELEASE=/var/lib/graphx/runtime/p9/installed-lab \
  GRAPHX_GUEST_RELEASE=/var/lib/graphx/runtime/p9/guests \
  GRAPHX_PRIVILEGED_EVIDENCE=/var/lib/graphx/runtime/p10/privileged-sequential \
  scripts/test-linux-network-features.sh
```

Use an absent evidence directory for a repeat run. The verified P9 native/guest
artifacts exercise unchanged production C++ execution. P10 changes launch/test
selection, documentation and web fallback behavior. Private credentials, QMP
state and high-I/O captures remain in guest storage. Public before/after
inventories and cleanup logs accompany the per-case results under
`outputs/p10/acceptance`. All successful cases passed their owned cleanup checks.

The first concurrent inventory runs were discarded after a portable sentinel
appeared during an OVS case; the matrices were rerun sequentially against each
engine. Only the sequential results are counted above.

## Remaining release gates

- Native Linux x86_64 acceptance is unavailable on this macOS ARM64 workspace and
  its existing ARM64 Lima VM. Lima TCG does not replace that host coverage.
- Clean release construction, independent repeat-build reproducibility and final
  release-specific image/companion/guest qualification remain required from the
  final source revision. Existing verified development artifacts and package tests
  are not a published release claim.
- Browser evidence is desktop Safari on a native test graph. It does not establish
  mobile layout or browser rendering of every OVS/guest topology.
- Physical radio startup remains gated on a separate uplink ownership contract;
  KVM is outside the accepted TCG implementation.
