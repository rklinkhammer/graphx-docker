# QEMU node

The [TAP profile](tap/README.md) declares an x86_64 TCG guest, namespace peer,
OVS VLANs and bounded mirror capture. Compile it with a verified guest release
catalog and run the compilation through the common owned runner.

The [guest release guide](../../guests/README.md) describes the shared echo/radio
Buildroot recipe, artifact verification and combined native/guest installation.
The source catalog contains unavailable illustrative guest pins; it cannot boot.

```sh
examples/qemu-node/scripts/build.sh --catalog "$IMAGE_CATALOG" --output "$GUEST_CANDIDATE"
```

`examples/qemu-node/scripts/demo.sh start|status|stop` maps to common
`run up|status|down`. Set `GX_OUTPUT`, `GX_STATE`, `GX_RELEASE`,
`GRAPHX_IMAGE_RELEASE`, and, only after explicit authorization,
`GRAPHX_ALLOW_PRIVILEGED=1`. The TAP wrapper also accepts `plan|up|status|down`.
Run on native Linux or explicitly inside the existing GraphX Lima guest. These
wrappers do not provision a VM or invoke sudo.

Runtime state and high-I/O artifacts stay under `/var/lib/graphx`. No privileged
socket is forwarded to macOS. Scenario actions, including automated
broadcast/multicast and pause/resume verification, remain a P9 gate. See
[P8 verification](../../design/graph-generation/p8-verification.md) for actual
boot evidence and unrun environments; TAP lifecycle tests alone prove no guest boot.
