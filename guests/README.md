# Managed guest artifacts

The common Buildroot recipe builds two x86_64 Linux initramfs artifacts:
`echo-x86` runs `graphx-packet-guest` (`guest.echo`), and `radio-x86` runs the
shared SDR application as `sdr.radio`. Both include the authoritative C++
`graphx node-settings` reader and the same bounded virtio provisioning agent.
Neither artifact contains a topology, address, credential or management NIC.

Build on Linux or the existing GraphX Lima guest, with a ready Docker engine and
guest-local storage. This build runs without Docker `--privileged`, host networking,
device mounts or root inside the builder. Dependency archives are fetched and
checksum-checked first; compilation and packaging use Docker `--network none`.
The yaml-cpp input comes from the URL and checksum in the authoritative CMake file. On macOS, direct builds require OrbStack.

```sh
python3 scripts/release/guest_release.py build \
  --catalog /var/lib/graphx/releases/images/catalog \
  --output /var/lib/graphx/releases/guest-candidate
python3 scripts/release/guest_release.py verify \
  --guests /var/lib/graphx/releases/guest-candidate
python3 scripts/release/guest_release.py install \
  --native /var/lib/graphx/releases/native-with-companion \
  --guests /var/lib/graphx/releases/guest-candidate \
  --output /var/lib/graphx/releases/combined
```

Use explicit `--allow-dirty` for a development candidate. `build --work-dir DIR`
can reuse a development Buildroot cache only with that flag. A cache-assisted
candidate does not establish independent repeat-build reproducibility. The output
directory must be absent. The builder reuses its toolchain between the two fixed
application recipes and rebuilds the GraphX package for each application.

Each installed guest directory has exactly `bzImage`, `rootfs.cpio.gz`,
`licenses.json`, and `artifact-manifest.json`. The generated catalog pins all four.
Provenance records the source tree digest (paths, modes and file bytes), builder
manifest digest, x86_64 architecture, source epoch, literal build argv, application
contract and source commit. `licenses.json` records Buildroot's legal inventory and an embedded SPDX 2.3 SBOM.
The verifier also parses the bounded initramfs without extraction, checks the
packaged x86_64 ELF executables and their runtime-library inventory, application
identity, protected paths and agent source hashes. GraphX and yaml-cpp link statically
into the two executables; an external application-library dependency is rejected. The installer checks native/guest commit agreement and creates a new combined
release receipt. Container images remain separately verified OCI artifacts.

The source catalog's illustrative guest outputs remain unavailable. Compile with
the generated guest candidate's catalog, then use the combined release with
`graphx run`. Missing, substituted or unverified artifacts fail before TAP creation.

The common runner starts QEMU as the existing dedicated `graphx-qemu` account,
UID/GID 65532, with TCG, a single owned TAP, no default devices, local QMP and a
1 MiB serial ring. It records the PID/start/executable identity and private runtime
directory inode in the common ownership ledger. Cleanup validates identities before
stopping processes, removes boot copies and sockets, and retains a sealed bounded
QEMU log. It does not use a separate launcher PID ledger.

Three local sockets deliver config, credentials and readiness. The host verifies
socket ownership, mode, inode and QEMU peer PID/UID, connects all channels, and waits
for QEMU to report the guest ports open before sending bytes. Messages
use four-byte big-endian lengths; provisioning is at most 1 MiB and readiness at
most 4096 bytes. The guest verifies node ID, config digest and invocation token,
validates the node through the C++ reader, writes credentials plus their verified
generation metadata only to an 8 MiB tmpfs with private modes, and starts the fixed
application at UID/GID 65532. Connectors remain held until listeners are ready.
Guest startup has a shared 120-second host deadline; peers in guest graphs have a
resolved 180-second release wait. A failed handshake reports the final 8 KiB of
serial output within a two-second diagnostic deadline. No guest heartbeat over
management is required.

Guest boot is a separate privileged acceptance operation. See
[P8 verification](../design/graph-generation/p8-verification.md) for current evidence
and [the test procedure](../docs/test-procedure.md) for authorization and environment
boundaries. S15/T03 boot results do not imply KVM support or P9 scenario execution.
