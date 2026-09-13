# P1 implementation verification

P1 cuts over to authored version 3 and normalized contract version 2. Validation,
inspection and normalization are implemented in the existing C++ library and thin
CLI. Compilation, graph launch, credential staging, OVS realization and guest
execution remain unavailable. The user authorized P1 separately from the design
review; P2–P10 are not implemented by this change.

The production catalog is under `config/catalog`; image and guest pins remain
illustrative until packaging. All 24 production inputs are `graphx.yml`. Telemetry
validates the closed normalized schema, presents generic instance bindings and
uses catalog control capability rather than hard-coded sample node IDs. Existing
standalone HTTP/security/history/capture tests exercise reusable modules; they do
not claim an orchestrated v3 deployment.

## Model clarification

I-12 is resolved: router interfaces can explicitly select a switch `port`.
The QEMU peer data/isolated interfaces select ports `peer`/`isolated` with VLANs
42/43. This avoids inferring a switch-port relationship from sample names.
The optional field is present in both authored schemas and the concrete QEMU input.
No newly blocking conflict remains. I-01–I-11 remain accepted.

The production catalog also preserves source control capability explicitly and
sets a bounded capture snap length. Neither requires a sample-name branch.
P1 normalization may enrich the design snapshots with closed defaults; exact
compiler artifact goldens and relocated byte reproducibility are the P3 gate.

## Evidence

All required P1 checks pass on the final implementation:

| Check | Result and evidence |
|---|---|
| `scripts/verify.sh quick` with Node 24 on PATH | **PASS**, 30/30 CTest cases; [log](../../outputs/verification/20260913T144553Z-quick.log) |
| `scripts/verify.sh quality` | **PASS**, LLVM 21 formatting, clang-tidy and cppcheck across production, apps and tests; [log](../../outputs/verification/20260913T144554Z-quality.log) |
| `scripts/verify.sh portable` with Node 24 on PATH | **PASS**, CTest, 37 explicit launcher/application gates, 92 telemetry tests, 17 web tests and Vite build; [log](../../outputs/verification/20260913T144714Z-portable.log) |
| `ctest --test-dir build/dev --output-on-failure -R '^graphx-package$'` | **PASS**, install/relocation including the authored graph and catalog, external C++ consumer, archive checks |
| C++ accepted input matrix | **PASS**, 63 supported target normalizations, N01–N15 exact code/path checks, 33 unsupported targets |
| Catalog boundaries | **PASS**, hash mismatch, symlink/escape, duplicate pin, malformed parameter default and guest artifact declaration rejected |
| Design artifact consistency | **PASS**, 24 cases, 15 negatives, 63 target sets, 865 YAML/JSON files, 494 relative links, 51 read-only Compose configurations; [results](evidence/static-results.json) |
| ShellCheck | **PASS**, exact invocation below; `-x -P SCRIPTDIR` follows each launcher's declared relative sources |
| `git diff --check` | **PASS** |

The normalized consumer rejects contract 1, unknown nested fields, oversized
collections/files and unsafe files. C++ rejects authored version 2, unknown or
mis-typed fields, YAML alias/tag/merge/duplicate constructs, protocol mismatches and invalid multicast/broadcast destinations,
cardinality/feedback violations, incompatible IPC placement, malformed addresses,
ambiguous attachments and missing or incompatible guest declarations. Credential
material is not opened or emitted during normalization. Shared endpoint bindings
are checked for consistency at both ends of every accepted connection.

Transport socket/queue tests, resource identity and rollback tests, TLS, capture
and raw SDR protocol checks remain active. Superseded launcher plan tests now
validate resolved resource contracts and explicit unavailable execution behavior.
Multiple guest instances receive hierarchy nodes and cannot inherit another
guest's unscoped runtime evidence.

Exact Node selection for required portable checks:

```sh
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
```

Exact ShellCheck command:

```sh
shellcheck -x -P SCRIPTDIR examples/capture/run.sh examples/external-ovs-boundary.sh examples/ipvlan-l2/scripts/down.sh examples/ipvlan-l2/scripts/status.sh examples/ipvlan-l2/scripts/up.sh examples/ipvlan-l3/scripts/down.sh examples/ipvlan-l3/scripts/status.sh examples/ipvlan-l3/scripts/up.sh examples/macvlan/scripts/down.sh examples/macvlan/scripts/status.sh examples/macvlan/scripts/up.sh examples/mixed-network/scripts/down.sh examples/mixed-network/scripts/status.sh examples/mixed-network/scripts/up.sh examples/network-lab-ovs.sh examples/qemu-node/scripts/build.sh examples/qemu-node/scripts/demo.sh examples/qemu-node/scripts/inspect-capture.sh examples/qemu-node/scripts/test.sh examples/qemu-node/tap/scripts/ovs-lab.sh examples/sample-pipeline/scripts/demo.sh examples/sdr-node/common/generate_tls.sh examples/sdr-node/common/lifecycle.sh examples/sdr-node/external/scripts/demo.sh examples/sdr-node/simulated/scripts/demo.sh examples/shared-memory/run.sh examples/static-route-policy/scripts/demo.sh examples/static-route-policy/scripts/inspect.sh examples/udp-broadcast/down-native-linux.sh examples/udp-broadcast/run-native-linux.sh examples/udp-broadcast/run.sh examples/udp-multicast/run.sh examples/udp-unicast/run.sh scripts/lib/demo-runtime.sh scripts/network-lab.sh scripts/test-runtime-features.sh scripts/test-telemetry-features.sh
```

All checks run unprivileged on native macOS ARM64. No Docker engine, OrbStack
workload, Lima VM, Linux infrastructure, TCG guest or KVM guest is started. No
live Linux/Lima, guest-boot or browser-manual acceptance is claimed.
