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

Only the standard Docker demo currently connects every node to the telemetry
collector and authenticated control plane. For the remaining examples, the
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
allows <http://localhost:8080>. Port 8080 must be free. Use a different `PORT`
and matching browser URL if another service owns it, and include that exact
origin in `GRAPHX_ALLOWED_ORIGINS` for a Compose deployment.

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

Generate three distinct values. Reusing one value for multiple roles is rejected:

```sh
export GRAPHX_OBSERVATION_TOKEN="$(openssl rand -hex 32)"
export GRAPHX_CONTROL_TOKEN="$(openssl rand -hex 32)"
export GRAPHX_TELEMETRY_SHARED_SECRET="$(openssl rand -hex 32)"
```

For a local demonstration, display the two browser values so they can be copied
into the password fields. Do not print the runtime shared secret:

```sh
printf 'Observation token: %s\nControl token: %s\n' \
  "$GRAPHX_OBSERVATION_TOKEN" "$GRAPHX_CONTROL_TOKEN"
```

These are local demonstration credentials. For multi-operator policy files,
per-node runtime identities, rotation, and audit authorization, follow
[`control-plane.md`](control-plane.md).

## 3. Standard TCP Docker demo: full graphical operation

This is the primary graphical example and the only example in this guide with
live browser metrics and runtime controls.

1. Generate the three credentials as shown above.
2. Enable bounded application capture and start the stack:

   ```sh
   export GRAPHX_CAPTURE_ENABLED=true
   scripts/demo.sh start
   ```

3. Open <http://127.0.0.1:8080>.
4. Enter `GRAPHX_OBSERVATION_TOKEN` in **Observation token**. The topology and
   live updates should appear.
5. Enter `GRAPHX_CONTROL_TOKEN` in **Control token**.
6. Select **Application**, then select `samples` or `transformed`. Confirm that
   message counts and rates increase and recent message identities appear.
7. Select **Network path**. The selected logical edge is shown through the
   configured Docker bridge.
8. Select **Pause source**. Wait for the command status to become `accepted` and
   confirm the counters stop after in-flight work drains.
9. Select **Resume**. Confirm that counters continue without sequence reset.
10. Select **Reset counters**. This clears collector aggregation but does not
    stop the graph.
11. Select an application edge and use **Download GraphX** to save a source or
    destination PCAPNG file.
12. Re-run the command-line health check from the same credential-bearing shell:

    ```sh
    scripts/demo.sh verify
    ```

13. Stop the stack:

    ```sh
    scripts/demo.sh stop
    ```

The named capture volume survives `stop`. After downloading required evidence,
delete it deliberately with `docker compose down -v` if retention is unnecessary.

## 4. Reusable topology-only console

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

## 5. Standalone application-capture example

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

## 6. Shared-memory example

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

## 7. UDP unicast example

1. Set `CONFIG="$PWD/examples/udp-unicast/graphx.yaml"` in the topology-console
   recipe and start it.
2. Open **Application** and select the `messages` edge. Confirm transport `udp`,
   destination `subscriber:47101`, and schema `UdpExample`.
3. In another terminal, run `examples/udp-unicast/run.sh`.
4. Confirm `PASS received=5` in the terminal.

The current UDP example uses console logging rather than telemetry WebSocket
export, so browser counters remain unavailable.

## 8. UDP multicast example

1. Set `CONFIG="$PWD/examples/udp-multicast/graphx.yaml"` in the topology-console
   recipe and start it.
2. Select the `messages` edge and confirm the multicast topology.
3. In another terminal, run `examples/udp-multicast/run.sh`.
4. Confirm that two `PASS received=5` lines demonstrate loopback fan-out.

The browser represents one logical publisher-to-subscriber edge. The diagnostic
second subscriber is network fan-out and is intentionally not a second graph edge.

## 9. UDP broadcast example

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

## 10. Macvlan example

Native Linux is required for the runtime lab.

1. Set `CONFIG="$PWD/examples/macvlan/graphx.yaml"` in the topology-console recipe.
2. Open **Network path** and select each edge. Confirm the three application
   nodes traverse `gx-macvlan-demo`. Use `graphx.yaml` or `status.sh` for the
   exact IP/MAC assignments; application cards show deployment images.
3. Start the lab in another terminal:

   ```sh
   examples/macvlan/scripts/up.sh
   examples/macvlan/scripts/status.sh
   ```

4. Inspect sink values with `docker logs -f gx-mac-sink-sink-1`.
5. Stop with `examples/macvlan/scripts/down.sh`.

This example has no SPAN capture helper and no live console telemetry connection.

## 11. IPvlan L2 example

Native Linux is required. This is the richest native network-path display after
the mixed example.

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
   examples/ipvlan-l2/scripts/up.sh
   examples/ipvlan-l2/scripts/status.sh
   ```

5. In a third terminal, save an Ethernet mirror capture:

   ```sh
   examples/ipvlan-l2/scripts/capture.sh transform \
     "$PWD/captures/ipvlan-l2-console/transform-span.pcapng"
   ```

6. Let traffic flow briefly and stop capture with `Ctrl-C`.
7. Select either edge in the console and use **Download Ethernet**. The capture
   catalog refreshes at most once per second.
8. Stop with `examples/ipvlan-l2/scripts/down.sh`.

## 12. IPvlan L3 example

Native Linux is required.

1. Set `CONFIG="$PWD/examples/ipvlan-l3/graphx.yaml"` in the topology-console
   recipe.
2. Open **Network path** and verify that all three distinct subnets use the one
   `gx-ipvl3-domains` L3 parent path.
3. Start and inspect the lab:

   ```sh
   examples/ipvlan-l3/scripts/up.sh
   examples/ipvlan-l3/scripts/status.sh
   ```

4. Stop with `examples/ipvlan-l3/scripts/down.sh`.

IPvlan L3 has no L2 broadcast and this example has no OVS SPAN capture helper.

## 13. Mixed macvlan/IPvlan and OVS example

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
3. Start the appropriate runtime lab in another terminal.

   Native Linux:

   ```sh
   examples/mixed-network/scripts/linux-up.sh
   examples/mixed-network/scripts/status.sh
   ```

   Docker Desktop on macOS:

   ```sh
   examples/mixed-network/scripts/macos-up.sh
   examples/mixed-network/scripts/status.sh
   ```

4. Optionally apply and clear a network fault:

   ```sh
   examples/mixed-network/scripts/fault.sh apply
   examples/mixed-network/scripts/fault.sh clear
   ```

   Fault injection is intentionally not controlled by the browser button.

5. Save one or both Ethernet mirrors in separate terminals:

   ```sh
   examples/mixed-network/scripts/capture.sh mac \
     "$PWD/captures/mixed-console/mac-span.pcapng"
   examples/mixed-network/scripts/capture.sh ipv \
     "$PWD/captures/mixed-console/ipv-span.pcapng"
   ```

6. Stop each capture with `Ctrl-C`. Select an edge and use **Download Ethernet**.
7. Stop the lab with `linux-down.sh` or `macos-down.sh`, matching the start command.

The macOS profile validates the userspace OVS/routing shape; it does not certify
native macvlan or IPvlan semantics.

## 14. Inspect captures in Wireshark

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

## 15. Clean up credentials and generated data

After completing the examples:

```sh
unset GRAPHX_OBSERVATION_TOKEN GRAPHX_CONTROL_TOKEN
unset GRAPHX_TELEMETRY_SHARED_SECRET GRAPHX_CAPTURE_ENABLED GRAPHX_CAPTURE_DIR
```

Use each example's matching teardown helper before removing Docker networks.
Remove capture directories only after preserving required evidence.
