# Native-Linux external-SDR profile

This profile proves the same logical graph through Open vSwitch `br-sdr`. A
disposable Linux namespace runs the shared SDR simulator at `10.63.0.10`; Docker
processor and sink services join the external macvlan network at `.20` and
`.30`. OVS mirrors all ports to `sdr-cap`, where bounded `tcpdump` capture feeds
the shared packet observer.

## Prerequisites and warning

Use a native Linux host with Docker/Compose, Open vSwitch, iproute2, tcpdump,
OpenSSL, curl, Python 3, and working `sudo`. Build GraphX first with a supported
CMake preset. Set `GRAPHX_BIN=/absolute/path/to/graphx` if the executable is not
under a detected `build` directory.

The script creates host interfaces, `br-sdr`, namespace `gx-sdr-device`, Docker
network `gx-sdr-native`, and a mirror. These names must be unused. It does not
attach a physical interface. Read the physical contract in the suite README
before adapting the lab.

Every created bridge, mirror, interface, namespace, and Docker network carries
a random per-run ownership marker. Normal cleanup validates those intrinsic
markers before invoking any fixed-name teardown. If a state file is stale or a
same-named resource lacks the matching marker, cleanup refuses the operation;
inspect ownership manually instead of renaming, adopting, or deleting it.

## Run and verify

```bash
examples/sdr-node/external/scripts/demo.sh start
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh verify
```

Use the same GUI and control workflow described by the simulated profile at
<http://127.0.0.1:8080>. The Network tab must show `sdr-node` as host/external,
`br-sdr` as OVS infrastructure, and processor/sink as managed containers. Verify
that live counters advance, packet history is non-empty, and capture downloads
remain usable across tab changes.

Independent native inspection should include:

```bash
sudo ovs-vsctl show
sudo ovs-vsctl list mirror
ip -d link show sdr-cap
sudo ip netns exec gx-sdr-device ip address show
```

Inspect `outputs/sdr-node/external/<run>/sdr-node.pcapng` with filters
`udp.port == 18400 || tcp.port == 18401 || tcp.port == 18402`. Direct device
control uses `demo.sh control status|start|stop|tune HERTZ`.

Always stop the lab before switching network examples:

```bash
examples/sdr-node/external/scripts/demo.sh stop
```

After stopping, independently confirm `br-sdr`, `gx-sdr-device`, `sdr-cap`, and
`gx-sdr-native` are absent. Evidence is retained. If an interrupted start leaves
resources, rerun `stop`; cleanup targets only validated process IDs and native
resources with the current run's marker. PID files are mode 0600 and owned by
the invoking operator even though tcpdump and the namespace simulator require
sudo. Repeated `stop` succeeds when the owned resources are already absent.
Native Linux runtime output is required for OVS/SPAN acceptance. A real SDR is
optional and is never implied by simulator success.
