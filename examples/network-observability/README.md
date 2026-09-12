# Declarative network observation and faults

Platform: native Linux with sudo and system OVS, or the dedicated
[GraphX Lima guest](../../infrastructure/lima/README.md) on Apple Silicon macOS.
The GraphX CLI does not automatically dispatch to Lima.

This configuration creates an owned OVS bridge, TAP, mirror capture, and a
30-second netem fault. Its external QEMU node is a declaration; `infra create`
does not boot a VM or generate packets. Use the QEMU TAP example for guest traffic.

From the repository root, preview without privileges on either platform:

```sh
./build/dev/graphx infra create examples/network-observability/graphx.yaml --dry-run
```

For live execution on macOS, first provision and verify Lima, then enter it:

```sh
limactl shell --workdir /workspace/graphx-docker graphx
export GRAPHX_BIN=/var/lib/graphx/runtime/build/dev/graphx
```

On native Linux set `GRAPHX_BIN="$PWD/build/dev/graphx"`. In either Linux runtime:

```sh
sudo "$GRAPHX_BIN" infra create examples/network-observability/graphx.yaml
sudo "$GRAPHX_BIN" infra status examples/network-observability/graphx.yaml
# After 30 seconds, status should report the fault as expired.
sudo "$GRAPHX_BIN" infra destroy examples/network-observability/graphx.yaml
```

Application captures contain GraphX envelopes as LINKTYPE_USER0. OVS mirror
captures contain Ethernet PCAPNG; these evidence sources remain separate trust
domains. Destroy seals and retains bounded capture evidence under
`/var/lib/graphx/captures`; it removes the owned live infrastructure.
