# P8 owned guest verification

P8 implementation, unprivileged verification and **authorized S15/T03 actual guest
acceptance passed in the existing GraphX Lima VM** (Linux ARM64 host, x86_64 guest,
TCG). Both cases restored the original container, network, bridge, namespace, link
and managed-process inventories. Native Linux x86_64 acceptance remains unrun, so
the full cross-host P8 verification gate is still open.

## Implemented contract

- The common process ledger owns QEMU PID/start/executable identity and its private
  runtime directory inode. Cleanup validates identities before stopping resources;
  boot copies and sockets are removed, and bounded logs are retained.
- QEMU uses the existing `graphx-qemu` UID/GID 65532 account, x86_64 TCG, one owned
  TAP, local QMP, a 1 MiB serial ring and three bounded, peer-checked virtio channels.
  The host connects all channels and waits for guest-open status before provisioning.
  Handshake failures retain the last 8 KiB of serial output within a two-second
  diagnostic deadline. Configuration, credential generations and readiness carry node/digest/token identity.
  Guest credentials stay in an 8 MiB tmpfs with private file modes.
- Echo and SDR radio recipes package the authoritative node reader and fixed
  application contracts. Verification checks kernel and ELF architecture, initramfs
  contents, protected paths, packaged agent source hashes, provenance and SPDX inventory.
  GraphX is linked statically into guest executables; ELF dependency checks reject
  unbundled application libraries and missing runtime-library members. Dependency
  archives are checked before offline compilation/packaging.
- Packet applications use resolved addresses, ports, source interfaces and UDP bounds.
  S15's isolated VLAN interface cannot silently become the peer's selected interface.
  Guest graphs resolve a 180-second release wait around the shared 120-second boot handshake.
- QEMU wrappers use the common compiled runner. The separate PID launcher and
  superseded guest/TAP test contract are removed. S11 stays external; S14 physical
  uplinks, KVM and P9 scenario execution remain gated.

## Completed checks

| Check | Result and evidence |
|---|---|
| Native macOS ARM64 quick | 39/39 passed; [log](../../outputs/verification/20260913T234435Z-quick.log) |
| Native macOS quality | Formatting, clang-tidy and cppcheck passed; [log](../../outputs/verification/20260913T234502Z-quality.log) |
| Native macOS portable | Passed C++, telemetry, web and native execution checks; [log](../../outputs/verification/20260913T234620Z-portable.log) |
| Linux ARM64 native release inside Lima, UID 501 | 41/41 CTests passed; [build/package log](../../outputs/p8-lima-acceptance/native-diagnostics.log) |
| Guest artifact build and verification, unprivileged Docker inside Lima | Passed actual initramfs/application/architecture checks; [build log](../../outputs/p8-lima-acceptance/guest-linked.log) |
| S15/T03 real-pin compilation and plan | Passed; [S15 plan](../../outputs/p8-lima-acceptance/acceptance-s15-linked/compiled/qemu-plan.json), [T03 plan](../../outputs/p8-lima-acceptance/acceptance-t03-linked/compiled/qemu-plan.json) |
| Missing privileged opt-in | Both reject with `E_PRIVILEGED_AUTHORIZATION`, creating no runtime state; [results and hashes](../../outputs/p8-lima-acceptance/preflight-linked/results.json) |
| S15 Lima ARM64 → x86_64 TCG | Actual boot, QMP identity, UID 65532, one TAP, bidirectional TCP/UDP and cleanup passed; [results](../../outputs/p8-lima-acceptance/acceptance-s15-linked/results.json), [serial](../../outputs/p8-lima-acceptance/acceptance-s15-linked/qemu-node-serial.log) |
| T03 Lima ARM64 → x86_64 TCG | Actual radio samples/results, authenticated tune/start/stop, independent container radio and cleanup passed; [results](../../outputs/p8-lima-acceptance/acceptance-t03-linked/results.json), [control](../../outputs/p8-lima-acceptance/acceptance-t03-linked/sdr-control.json) |
| Design package | 24 positive cases, 15 negatives, 63 target sets and 33 unsupported-target diagnostics passed static checks; this does not establish runtime execution |
| ShellCheck | Passed the exact command below |

Focused tests cover missing/tampered artifacts, exact `E_GUEST_UNAVAILABLE` for a
missing binary, unsafe paths, architecture/provenance/plan mismatch, missing guest
software, wrong application identity, frame bounds/deadlines, duplicate fields,
cross-node credentials, generation hashes, private modes, renamed packet bindings,
release barriers and UDP size limits. One portable run observed a transient 503 in
an existing telemetry readiness test; its focused rerun and the full rerun passed.

## Reviewable development artifacts

The guest-local combined installation is
`/var/lib/graphx/runtime/p8/installed-linked`; guest artifacts/catalog/source are
under `/var/lib/graphx/runtime/p8/guest-linked`. The image input remains the verified
P7 `/var/lib/graphx/runtime/p7-acceptance/images-store`. The
[combined receipt](../../outputs/p8-lima-acceptance/installed-linked/release.json)
and [native manifest](../../outputs/p8-lima-acceptance/native-diagnostics/graphx-1.1.0-linux-aarch64.manifest.json)
record actual file identities. Kernels, root filesystems and build caches stay in Lima.
Only public plans, manifests and verification logs were copied to the workspace.

These are explicit `--allow-dirty` development candidates at commit
`1de032e2e8291c33ce7a3249a3c5f2003fa1283d`. A fresh C++ toolchain was built; final
application packaging reused that development cache. Independent clean repeat-build
reproducibility is not established. The source catalog's illustrative guest pins
remain unavailable; no release was published.

## Authorized live acceptance

The reviewable harness is `tests/test_guest_execution_live.py`. It requires
`--allow-privileged`, Linux root, verified guest/native/image artifacts, the existing
QEMU account, a ready Docker engine and system OVS. It does not provision a VM,
change Docker context or forward privileged sockets. On the authorized Linux guest,
the following case arguments were run through `limactl shell --workdir
/workspace/graphx-docker graphx -- sudo -n`. Repeating acceptance requires fresh
evidence directories:

```sh
python3 tests/test_guest_execution_live.py --allow-privileged --target lima \
  --case S15 --release /var/lib/graphx/runtime/p8/installed-linked \
  --images /var/lib/graphx/runtime/p7-acceptance/images-store \
  --guests /var/lib/graphx/runtime/p8/guest-linked \
  --output /var/lib/graphx/runtime/p8/acceptance-s15-linked
python3 tests/test_guest_execution_live.py --allow-privileged --target lima \
  --case T03 --release /var/lib/graphx/runtime/p8/installed-linked \
  --images /var/lib/graphx/runtime/p7-acceptance/images-store \
  --guests /var/lib/graphx/runtime/p8/guest-linked \
  --output /var/lib/graphx/runtime/p8/acceptance-t03-linked
```

S15 requires actual x86_64 agent/serial evidence, TCG, QEMU ownership, one TAP and
bidirectional guest/namespace TCP/UDP. T03 requires actual guest radio samples,
processor results, authenticated tune/start/stop and independent container-radio
behavior. Both compare infrastructure/process inventories after common cleanup.
Native Linux x86_64 TCG, KVM and browser acceptance remain unrun. Lima TCG boot
is established by the live reports and serial/application evidence, independently
of artifact inspection and TAP lifecycle checks. The original Docker workloads
were empty; both cases preserved existing volumes and retained owned history and
capture evidence in the Linux guest.

```sh
shellcheck -x -P . examples/qemu-node/scripts/build.sh examples/qemu-node/scripts/demo.sh examples/qemu-node/scripts/test.sh examples/qemu-node/scripts/inspect-capture.sh examples/qemu-node/tap/scripts/ovs-lab.sh guests/buildroot-external/board/overlay/etc/init.d/S80graphx
```
