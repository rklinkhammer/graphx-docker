# P5 verification — packaging and authored example

Status: **P5 complete for normal ARM64 image packaging and the ordinary CLI path
in the dedicated GraphX Lima guest**. The user explicitly authorized this P5
privileged verification. P6 remains unstarted. See [packaging design](packaging-design.md) and the
[example README](../../examples/four-radio-vita/README.md).

## Implemented surface

The authoritative catalog includes all four reusable VITA types and wire schemas.
`--with-vita` builds normal VITA images; private fault hooks require the separate
`--qualification-hooks` option. Verification binds that mode to packaged build
metadata and rejects unexpected qualification executables. The ordinary example
workflow selects the VITA role in a separate artifact cache and rejects hook-enabled
candidates for this example. VITA application compilation remains opt-in.

`examples/four-radio-vita/graphx.yml` authors four radios, processor/controller,
detector and passive recorder. It uses available startup, distinct control/data
ports, explicit source bindings on OVS, 9000-byte MTU, staged mTLS credentials,
N=2048 FFTs, authenticated console access and separate bounded diagnostic capture.
Compiler checks reject missing attachment/credential references, insufficient MTU
and the OrbStack target. Golden output covers the complete graph. No new schema,
source Compose overlay or production launcher was added.

## Exact release candidate

The local ARM64 candidate was built on OrbStack from `00d328991a03c3b21be58d96817ae9e4874f9273`
plus the P5 worktree changes (`dirty_candidate: true`). Every role was rebuilt
without cache and independently verified, including repeated archive identity,
file inventories, dependency pins, notices and SPDX inventories. Qualification
hooks are OFF. These are local OCI pins, not a registry publication claim.

| Role | OCI manifest digest |
| --- | --- |
| runtime | `sha256:6615b42a1ae5243f553b1f0dd13474dee0aaf8e971eeda709e601e2d7a847bfe` |
| sdr | `sha256:f9f6c9ed6d6074450594f6449aa23278a3fecb752498629149e8c045c1db8d4c` |
| telemetry | `sha256:b0e6c664cc79c2a90224c509ac663a7955134cc42f5b3118213bb1919e2a4f10` |
| vita | `sha256:39bb1a89cb1adb93f03787f6765365e8638df33ef0fb1b005ce2dd4008b6ba55` |

Derived catalog SHA-256: `c0440d2d4a4fcee24a1fa403e765d1f37cbd6a21105fee87b507b287847c5f3b`.

SoapySDR is pinned to `1cf5a539a21414ff509ff7d0eedfc5fa8edb90c6` (0.8.1,
BSL-1.0); vrt_framework to `dbe85d37155145842da60367af1c4beef8801b0c` (MIT).
FFT code is part of GraphX under MIT. The VITA role contains the four executables,
SoapySDR library, exact upstream notices and dependency metadata; the normal
image excludes the qualification-only recorder executable.

```sh
PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 scripts/release/image_release.py build \
  --with-vita --no-cache --allow-dirty --platform linux/arm64 \
  --output outputs/verification/p5/images
python3 scripts/release/image_release.py verify outputs/verification/p5/images
```

Evidence is under `outputs/verification/p5/`. The same verified archives and
catalog are staged at `/var/lib/graphx/verification/p5/images` in the identity-matched
GraphX Lima guest (fingerprint
`995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e`).

## Verification

Quick and portable verification each passed 42 CTests. Portable also passed 114
telemetry tests, 23 web tests, the console build and native lifecycle checks.
Quality passed formatting and static analysis. Compiler verification passed 63
target packages, 15 negative cases and 33 unsupported-target checks; the final
console-port adjustment passed the example test and regenerated compiler goldens.
Seven focused VITA processing/network/live-contract tests, documentation consistency
and release boundary tests passed. No shell scripts were changed.

The packaged P1 test passed in a fresh unprivileged Lima ARM64 container; the native
macOS P1 test also passed (41.14 seconds). The packaged test received 20/512/20/20
data packets and 14,348/524,288/640/14,348 samples for radios 1–4, with strict
continuity, rational timing, burst framing, phase, restart and mTLS assertions.
See `packaged-p1-lima.log` and `native-p1-final.log`. A portable lifecycle process
identity timing failure passed on focused retry and in the complete final portable
run (`portable-final.log`); no lifecycle code change was made.

The packaged P1 harness extracts hash-verified radio and SoapySDR bytes
from the VITA archive and runs them with the verified SDR role's Python/OpenSSL
in a new, unprivileged, network-none Linux container. It supplies an invalid private
fault-hook environment setting to confirm normal application behavior ignores it.
The test receiver requests a bounded 4 MiB socket buffer for traffic accumulated
while four TLS clients verify acknowledgment replays; no privileged override or
application behavior change is involved. P1's continuity, phase, exact timestamp,
burst marker and mTLS assertions remain strict. OrbStack scheduling does not
provide reliable loss-free P1 timing evidence; the dedicated Lima result is separate.

## Authorized ordinary CLI execution

The separately authorized P5 run used instance `four-radio-vita-p5-normal`, graph
`four-radio-vita-p5-normal-7edffad020`, and fresh containers from the verified normal
images above. Its 17 compiled artifacts are under
`/var/lib/graphx/examples/lima/four-radio-vita-p5-normal/7edffad020634e42b08ac9414019d104/compiled`.
The subnet is `10.79.0.0/24`; guest loopback console port 8080 uses the standard
Lima forwarding to Mac loopback port 18080.

| Check | Observed result |
| --- | --- |
| `example plan`, `prepare`, `up` | Seven applications plus the platform; all seven admitted ready; normal image identities matched the verified manifest |
| `example status` while running | Ready, current configuration matched, all processes and infrastructure identity-owned |
| `example logs` | Detector, processor and recorder logs retrieved through the ordinary CLI |
| Radio control | All four mTLS sessions connected from `10.79.0.14` to `.10`–`.13`, ports 18401–18404; generated credentials staged by the common lifecycle |
| Data paths | Processor listeners `.14:18501`–18504 and detector `.15:18600`; radio UDP source sockets bound to `.10`–`.13`; data interfaces MTU 9000 |
| Management separation | One separate management network per application, no published application ports; only telemetry sockets used management addresses; actual control/data sockets used owned OVS attachments |
| Processing | All four streams available; observed processor count 134,184 spectra, zero send drops, invalid samples, duplicates or sequence gaps in that snapshot |
| Detection | Four 2048-bin results at 100049804.6875, 100100097.65625, 100149902.34375 and 100200195.3125 Hz; each within half a bin of its authored tone |
| Passive recorder | Observed 406,252 packets / 2,312,354,316 bytes; zero truncation, invalid frames, kernel drops or errors; `persistence=none` |
| Process restrictions | All application effective/permitted/inheritable/ambient capability sets zero after startup; no-new-privileges retained; recorder passive interface unaddressed for IPv4 |
| Console | Mac forwarding worked; anonymous `/api/topology` returned 401, authenticated request returned 200 |
| Diagnostic capture | Independent OVS mirror active; two retained PCAPNG files within the authored 4 MiB rotation bound |
| `example down` and cleanup | Exit 0; no remaining containers, OVS bridges, owned links, application networks or release barrier; only the standard history volume and bounded capture evidence retained |

Evidence: `example-up.log` (tokens redacted), `example-status.json`,
`example-{detector,processor,recorder}.log`, `live-inspection.json`,
`network-runtime.json`, `console-auth.json`, `example-down.log`, and `cleanup.json`
under `outputs/verification/p5/`. Raw capture and high-I/O runtime state remain
guest-local. Post-stop `run status` returns unavailable/exit 2; the example wrapper
reports that nonzero status as a command failure. Cleanup was independently
checked against the ownership ledger and engine inventories.

This is functional packaging and CLI evidence. Radios reported overdue-sample
skips (9,216 / 3,072 / 10,240 / 9,216 in the retained inspection), with zero UDP
errors or clipping. Do not infer sustained loss-free acceptance from readiness or
the processing snapshot. P6 sustained acceptance and browser interaction remain
unrun. No native Linux host, QEMU guest boot, TCG, KVM or registry publication result
is claimed.

```sh
build/rebuild-20260920/dev/graphx example plan four-radio-vita --target lima --json
build/rebuild-20260920/dev/graphx example up four-radio-vita --target lima \\
  --allow-privileged --instance four-radio-vita-p5-normal \\
  --images /var/lib/graphx/verification/p5/images --no-open --json
build/rebuild-20260920/dev/graphx example status four-radio-vita --target lima \\
  --allow-privileged --instance four-radio-vita-p5-normal --json
build/rebuild-20260920/dev/graphx example logs four-radio-vita --target lima \\
  --allow-privileged --instance four-radio-vita-p5-normal --node detector
build/rebuild-20260920/dev/graphx example down four-radio-vita --target lima \\
  --allow-privileged --instance four-radio-vita-p5-normal
```

```sh
GRAPHX_DEV_BUILD_DIR=build/rebuild-20260920/dev scripts/verify.sh quick
# quality.log records LLVM 21 with the CommandLineTools MacOSX26 SDK.
scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH \\
  GRAPHX_BUILD_DIR="$PWD/build/rebuild-20260920/dev" scripts/verify.sh portable
python3 tests/test_image_release.py .
python3 tests/test_vita_example.py build/rebuild-20260920/dev/graphx .
python3 tests/test_compile.py build/rebuild-20260920/vita/graphx .
ctest --test-dir build/rebuild-20260920/vita -R '^graphx-vita-standalone$' --output-on-failure
# In the identity-matched guest; fresh container, no network privileges:
python3 tests/test_vita_packaged.py /var/lib/graphx/verification/p5/images
```
