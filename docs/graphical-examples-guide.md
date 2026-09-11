# GraphX graphical examples, control, and capture guide

This guide explains how to display each GraphX example in the browser console,
which examples provide live telemetry and control, and how to obtain application
or Ethernet PCAPNG captures. Run commands from the repository root unless a step
says otherwise.

## Capability overview

| Example | Console topology | Live console metrics | Pause/resume | Capture |
|---|---|---|---|---|
| Standard TCP Docker demo | Yes | Yes | Yes | GraphX application frames |
| Standalone capture demo | Yes | No | No | GraphX application frames |
| Shared memory | Yes | No | No | Not configured |
| UDP unicast | Yes | No | No | Not configured |
| UDP multicast | Yes | No | No | Not configured |
| UDP broadcast | Yes | No | No | Native acceptance can capture packets |
| Macvlan | Yes, including network path | No | No | External tools only |
| IPvlan L2 | Yes, including OVS/router path | No | No | OVS Ethernet mirror |
| IPvlan L3 | Yes, including network path | No | No | External tools only |
| Mixed macvlan/IPvlan | Yes, including OVS/router path | No | No | OVS Ethernet mirrors |
| External QEMU | Yes, including host/VM path | Yes, passive packets | Origin only | Ethernet PCAPNG |
| Linux containerized QEMU | Yes, including container/VM path | Yes, passive packets | Origin only | Ethernet PCAPNG |
| Simulated SDR | Yes, including bridge path | Yes, passive packets | Processor relays to SDR | Ethernet PCAPNG |
| External SDR | Yes, including OVS path | Yes, passive packets | Processor relays to SDR | OVS Ethernet PCAPNG |

The standard Docker demo, both QEMU demos, and both SDR profiles connect to the telemetry collector
and authenticated control plane. The QEMU controls apply only to their raw
traffic origin, not the guest. For the remaining examples, the
console accurately renders `graphx.yaml`, but node cards remain starting/offline
and counters remain zero or unavailable. Their launch helpers intentionally test
the transport or network laboratory without adding a management network.

## 1. Prepare the console

Install the browser and telemetry dependencies and build the console once:

```sh
npm ci --prefix apps/telemetry
npm ci --prefix web
npm run build --prefix web
```

For examples that use native executables, also build GraphX:

```sh
scripts/verify.sh quick
```

The console is served at <http://127.0.0.1:8080>; the standard Compose demo also
allows <http://localhost:8080>. If another service owns port 8080, run
`GRAPHX_PUBLISHED_HTTP_PORT=18080 scripts/demo.sh start` and use the printed
console URL. The guided script applies the port to Docker publication, health
checks, and allowed browser origins together.

## 2. Understand the three credentials

GraphX keeps observation, operator control, and runtime authentication separate:

- **Observation token** protects topology, metrics, history, WebSocket updates,
  capture listings, and capture downloads. Enter it in the browser's
  **Observation token** field.
- **Control token** authorizes pause, resume, reset, and command-status requests.
  Enter it in the browser's **Control token** field. The browser keeps it only in
  memory.
- **Telemetry shared secret** authenticates runtime telemetry and control
  acknowledgements in the simple, single-operator demo. It is not entered into
  the browser.

The guided Docker demo automatically generates distinct control and runtime
credentials in `.graphx/demo.env`. Retrieve the browser value with:

```sh
scripts/demo.sh token
```

The observation token remains optional for the loopback-only demo. To test a
protected read-only console, generate it separately before startup:

```sh
export GRAPHX_OBSERVATION_TOKEN="$(openssl rand -hex 32)"
```

These are local demonstration credentials. For multi-operator policy files,
per-node runtime identities, rotation, and audit authorization, follow
[`control-plane.md`](control-plane.md).

## 3. Standard TCP Docker demo: full graphical operation

This is the primary graphical example and the only example in this guide with
live browser metrics and runtime controls.

1. Start the stack. Bounded application capture and SQLite history are enabled
   by default, and the generated control token is printed after verification:

   ```sh
   scripts/demo.sh start
   ```

2. Open the console URL printed by the script; the default is
   <http://127.0.0.1:8080>.
3. If you explicitly configured `GRAPHX_OBSERVATION_TOKEN`, enter it in
   **Observation token**. Otherwise leave that field empty.
4. Paste the value printed by `scripts/demo.sh token` into **Control token**.
5. Select **Application**, then select `samples` or `transformed`. Confirm that
   message counts and rates increase and recent message identities appear.
6. Select **Network path**. The selected logical edge is shown through the
   configured Docker bridge.
7. Select **Pause source**. Wait for the command status to become `accepted` and
   confirm the counters stop after in-flight work drains.
8. Select **Resume**. Confirm that counters continue without sequence reset.
9. Select **Reset counters**. This clears collector aggregation but does not
    stop the graph.
10. Select an application edge and use **Download GraphX** to save a source or
    destination PCAPNG file.
11. Re-run the command-line health check from the same credential-bearing shell:

    ```sh
    scripts/demo.sh verify
    ```

12. Stop the stack:

    ```sh
    scripts/demo.sh stop
    ```

The named capture volume survives `stop`. After downloading required evidence,
delete it deliberately with `docker compose down -v` if retention is unnecessary.

## 4. QEMU demos: raw traffic with live graphics

Build the common x86_64 guest once:

```sh
examples/qemu-node/scripts/build.sh
```

For macOS or portable host-managed QEMU:

```sh
examples/qemu-node/external/scripts/demo.sh start --accel auto
```

For native Linux with QEMU in Docker:

```sh
examples/qemu-node/container/scripts/demo.sh start --accel kvm
```

Open <http://127.0.0.1:8080/>. Application shows the same origin → QEMU →
receiver graph in both demos. Network shows the profile-specific host or nested
container/VM boundary. Counters come from passive Ethernet observation and
update over WebSocket. History shows bounded packet metadata rather than GraphX
envelope history. Downloaded QEMU captures use Ethernet link type 1.

Retrieve the profile's token, paste it into **Control token**, and use Pause or
Resume to control `host-origin`. The QEMU guest remains explicitly
uncontrollable. Use the matching profile script's `verify`, `status`, `logs`,
and `stop` commands. The complete guide and Linux operator acceptance procedure
are in [`qemu-demos.md`](qemu-demos.md).

## 4A. SDR demos: data, device control, and OVS capture

Start the portable profile on macOS or Linux:

```sh
examples/sdr-node/simulated/scripts/demo.sh start
```

Open <http://127.0.0.1:8080>, paste the token printed by `demo.sh token`, and
select each of the three edges. UDP `sdr-samples` should advance most quickly;
the TLS `processor-control` edge advances when status or control is used; and
`processed-results` follows accepted sample blocks. The Network view labels the
portable switch as a bridge simulation. Capture and packet history are enabled
by default and contain Ethernet packets, not GraphX message records.

Pause and Resume target the processor's registered control endpoint. The
processor sends an authenticated stop/start command to the SDR. Reset clears
collector counters, as in the standard demo; it does not reset or take ownership
of physical hardware. Directly prove the same device path with:

```sh
examples/sdr-node/simulated/scripts/demo.sh control status
examples/sdr-node/simulated/scripts/demo.sh control tune 433920000
```

On native Linux, stop this profile and run the `external` script instead. Its
Network view must show `sdr-node` as external, `br-sdr` as OVS infrastructure,
and processor/sink as containers. Only that run is evidence for OVS/SPAN. Full
commands, physical-device limits, inspection, and cleanup are in the
[`sdr-node` guide](../examples/sdr-node/README.md).

## 5. Reusable topology-only console

Use this recipe for examples whose launch scripts are not wired to the telemetry
collector. Replace `CONFIG` and `CAPTURE_DIR` with the values in the relevant
example section:

```sh
unset GRAPHX_OTLP_ENDPOINT GRAPHX_OTLP_AUTH_TOKEN_FILE
unset GRAPHX_OTLP_CA_FILE GRAPHX_OTLP_CERT_FILE GRAPHX_OTLP_KEY_FILE

CONFIG="$PWD/examples/shared-memory/graphx.yaml"
CAPTURE_DIR="$PWD/captures/console"
mkdir -p "$CAPTURE_DIR"

GRAPHX_CONFIG="$CONFIG" \
GRAPHX_WEB_ROOT="$PWD/web/dist" \
GRAPHX_CAPTURE_DIR="$CAPTURE_DIR" \
GRAPHX_OBSERVATION_TOKEN="$GRAPHX_OBSERVATION_TOKEN" \
GRAPHX_HTTP_BIND=127.0.0.1 \
GRAPHX_TELEMETRY_BIND=127.0.0.1 \
PORT=8080 GRAPHX_TELEMETRY_PORT=9000 \
  node apps/telemetry/server.mjs
```

Leave this terminal running, open <http://127.0.0.1:8080>, and enter the
observation token. Stop the topology console with `Ctrl-C`. Do not set a control
token for these examples: their runtime processes are not connected to this
collector, so pause/resume would not be meaningful.

## 6. Standalone application-capture example

This finite TCP example produces GraphX-framed PCAPNG files and then displays
them in the console catalog.

1. Choose a fresh directory and run the capture:

   ```sh
   export GRAPHX_CAPTURE_DIR="$PWD/captures/graphical-capture"
   examples/capture/run.sh
   ```

2. After the finite capture command completes, start the topology console in
   the same credential-bearing terminal:

   ```sh
   GRAPHX_CONFIG="$PWD/graphx.yaml" \
   GRAPHX_WEB_ROOT="$PWD/web/dist" \
   GRAPHX_CAPTURE_ENABLED=true \
   GRAPHX_CAPTURE_PROVIDER=pcapng \
   GRAPHX_CAPTURE_DIR="$PWD/captures/graphical-capture" \
   GRAPHX_OBSERVATION_TOKEN="$GRAPHX_OBSERVATION_TOKEN" \
   GRAPHX_HTTP_BIND=127.0.0.1 GRAPHX_TELEMETRY_BIND=127.0.0.1 \
   PORT=8080 GRAPHX_TELEMETRY_PORT=9000 \
     node apps/telemetry/server.mjs
   ```

3. Enter the observation token and select either edge.
4. Download `generator.pcapng`, `transform.pcapng`, or `sink.pcapng`.
5. Open the downloaded file in Wireshark with `wireshark/graphx.lua` installed.

The console shows the files from a completed run, but it has no historical
telemetry events with which to correlate the displayed packet numbers.

## 7. Shared-memory example

1. In the topology-console recipe, set:

   ```sh
   CONFIG="$PWD/examples/shared-memory/graphx.yaml"
   CAPTURE_DIR="$PWD/captures/shared-memory-console"
   ```

2. Open **Application** to see generator → transform → sink with both edges
   labeled `shared_memory`.
3. In another terminal, run `examples/shared-memory/run.sh`.
4. Confirm the terminal ends with the expected doubled sink value.

The run is observable in its terminal; it does not publish live events to the
browser console and does not create capture files.

## 8. UDP unicast example

1. Set `CONFIG="$PWD/examples/udp-unicast/graphx.yaml"` in the topology-console
   recipe and start it.
2. Open **Application** and select the `messages` edge. Confirm transport `udp`,
   destination `subscriber:47101`, and schema `UdpExample`.
3. In another terminal, run `examples/udp-unicast/run.sh`.
4. Confirm `PASS received=5` in the terminal.

The current UDP example uses console logging rather than telemetry WebSocket
export, so browser counters remain unavailable.

## 9. UDP multicast example

1. Set `CONFIG="$PWD/examples/udp-multicast/graphx.yaml"` in the topology-console
   recipe and start it.
2. Select the `messages` edge and confirm the multicast topology.
3. In another terminal, run `examples/udp-multicast/run.sh`.
4. Confirm that two `PASS received=5` lines demonstrate loopback fan-out.

The browser represents one logical publisher-to-subscriber edge. The diagnostic
second subscriber is network fan-out and is intentionally not a second graph edge.

## 10. UDP broadcast example

1. Set `CONFIG="$PWD/examples/udp-broadcast/graphx.yaml"` in the topology-console
   recipe and start it.
2. Select `messages` and confirm UDP broadcast port 47102.
3. In another terminal, prepare the image if necessary and run the isolated demo:

   ```sh
   docker build -t graphx-demo:latest .
   examples/udp-broadcast/run.sh
   ```

4. Confirm `PASS received=5`.

The Docker runner removes its internal network when complete. Native Linux live
capture is an acceptance operation, not a browser-console capture:

```sh
GRAPHX_BUILD_DIR="$PWD/build/dev" GRAPHX_VERIFY_LIVE_CAPTURE=1 \
  examples/udp-broadcast/run-native-linux.sh
examples/udp-broadcast/down-native-linux.sh
```

## 11. Macvlan example

The runtime lab uses native Linux directly or the GraphX Lima VM on Apple
Silicon macOS. Prepare Lima with `infrastructure/lima/start.sh` and
`infrastructure/lima/verify.sh` before the first macOS run.

1. Set `CONFIG="$PWD/examples/macvlan/graphx.yaml"` in the topology-console recipe.
2. Open **Network path** and select each edge. Confirm the three application
   nodes traverse `gx-macvlan-demo`. Use `graphx.yaml` or `status.sh` for the
   exact IP/MAC assignments; application cards show deployment images.
3. Start the lab in another terminal:

   ```sh
   scripts/network-lab.sh macvlan up
   scripts/network-lab.sh macvlan status
   ```

4. Require the `up` and `status` actions to report healthy infrastructure and
   advancing sink traffic.
5. Stop with `scripts/network-lab.sh macvlan down`.

This example has no SPAN capture helper and no live console telemetry connection.

## 12. IPvlan L2 example

Use native Linux directly or GraphX Lima on Apple Silicon macOS. This is the
richest network-path display after the mixed example.

1. Create the capture directory:

   ```sh
   mkdir -p "$PWD/captures/ipvlan-l2-console"
   ```

2. Start the topology console with:

   ```sh
   CONFIG="$PWD/examples/ipvlan-l2/graphx.yaml"
   CAPTURE_DIR="$PWD/captures/ipvlan-l2-console"
   ```

3. Open **Network path**. Select `samples` to see generator network → OVS →
   namespace router → OVS → transform. Select `transformed` to see the sink path.
4. Start and inspect the lab:

   ```sh
   scripts/network-lab.sh ipvlan-l2 up
   scripts/network-lab.sh ipvlan-l2 status
   ```

5. Select each edge and compare its OVS bridge, namespace-router hop, and policy
   state with the canonical `status.sh` output.
6. Stop with `scripts/network-lab.sh ipvlan-l2 down`.

Use `examples/network-observability` when the acceptance scope requires
declarative Ethernet capture or timed fault evidence.

## 13. IPvlan L3 example

Use native Linux directly or GraphX Lima on Apple Silicon macOS.

1. Set `CONFIG="$PWD/examples/ipvlan-l3/graphx.yaml"` in the topology-console
   recipe.
2. Open **Network path** and verify that all three distinct subnets use the one
   `gx-ipvl3-domains` L3 parent path.
3. Start and inspect the lab:

   ```sh
   scripts/network-lab.sh ipvlan-l3 up
   scripts/network-lab.sh ipvlan-l3 status
   ```

4. Stop with `scripts/network-lab.sh ipvlan-l3 down`.

IPvlan L3 has no L2 broadcast and this example has no OVS SPAN capture helper.

## 14. Mixed macvlan/IPvlan and OVS example

This example provides the most detailed network visualization and two Ethernet
mirror points.

1. Create a capture directory and use it in the topology-console recipe:

   ```sh
   CONFIG="$PWD/examples/mixed-network/graphx.yaml"
   CAPTURE_DIR="$PWD/captures/mixed-console"
   mkdir -p "$CAPTURE_DIR"
   ```

2. Open **Network path** and select `samples`. Confirm macvlan → OVS → namespace
   router → OVS → IPvlan. Select `transformed` to see the shorter IPvlan path.
3. Start the canonical runtime lab on native Linux or inside Lima:

   ```sh
   scripts/network-lab.sh mixed-network up
   scripts/network-lab.sh mixed-network status
   ```

4. Select edges and confirm their semantic domains, OVS bridges, namespace
   router, and policy state match `graphx inspect`.
5. Stop the lab with `scripts/network-lab.sh mixed-network down`.

Use `examples/network-observability` for declarative bounded Ethernet capture
and timed fault evidence. OrbStack is not a privileged network-lab backend;
macOS runs this system-OVS lifecycle inside Lima.

## 15. Static-route and deny-policy laboratory

Native Linux is required for runtime evidence; the portable inspection script
only validates and prints plans.

1. Start the lab with `examples/static-route-policy/scripts/demo.sh start` and
   open the loopback URL it prints.
2. In **Application**, verify `allowed-flow` is green, `denied-flow` is red and
   dashed, and `routed-flow` is amber and dashed. Select each edge to see
   `receiver-confirmed`, `nft-counter`, or `route-absent` evidence.
3. Open **Network path** and select each flow. Confirm that every path includes
   the matching source domain and OVS bridge, `route-router`, and the matching
   destination bridge/domain.
4. Run `examples/static-route-policy/scripts/demo.sh apply-route`. The routed
   path becomes green with `route-installed`; the other two classifications do
   not change. Run `clear-route` to restore the amber missing-route state.
5. Use the capture catalog to download `route-policy.pcapng`. The raw diagnostic
   traffic uses UDP ports 18601–18603 and standard Ethernet/IP decoding.
6. Run `demo.sh status`, then `demo.sh stop` twice. Evidence remains in the run
   directory printed during teardown.

No observation or control token is needed: the service is loopback-only and the
lab offers no browser control. Route mutation stays in the narrow native CLI.

## 16. Inspect captures in Wireshark

GraphX application captures and OVS Ethernet captures use different link types:

- **GraphX application capture** uses `LINKTYPE_USER0` (147) and needs the
  checked-in `wireshark/graphx.lua` dissector.
- **OVS/network capture** uses standard `LINKTYPE_ETHERNET` (1) and is decoded by
  ordinary Ethernet/IP/TCP dissectors.

Install the Lua plugin in the personal directory shown by `tshark -G folders`,
restart Wireshark, and use filters such as:

```text
graphx.version == 2
graphx.sequence == 5
graphx.type == "Sample"
```

PCAPNG files contain complete application payloads or network packets. Treat
them as sensitive evidence, limit access, and delete or archive them according
to the applicable retention policy. See [`capture.md`](capture.md) for format,
extcap, validation, size limits, and security details.

## 17. Clean up credentials and generated data

After completing the examples:

```sh
unset GRAPHX_OBSERVATION_TOKEN GRAPHX_CONTROL_TOKEN
unset GRAPHX_TELEMETRY_SHARED_SECRET GRAPHX_CAPTURE_ENABLED GRAPHX_CAPTURE_DIR
```

Use each example's matching teardown helper before removing Docker networks.
Remove capture directories only after preserving required evidence.
