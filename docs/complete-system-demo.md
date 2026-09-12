# Run the complete GraphX demo

This is the recommended first run. It starts the complete portable system: three
C++ node containers, their private Docker bridge, the telemetry collector, and
the browser console. You do **not** need to build C++, install npm packages, run
`graphx infra`, or start the advanced macvlan/ipvlan laboratories first.

This page covers the guided demo only. Use the
[`complete user guide`](user-guide.md) when creating a configuration, defining
nodes or networks, selecting capture, or operating telemetry outside this demo.

## What will run

```text
generator -- Sample(n) --> transform -- TransformedSample(2n) --> sink
     \________________ TCP on Docker's private graphx bridge __________/
                                |
                     UDP telemetry events
                                v
                 telemetry API + WebSocket + console
```

The generator emits one sample every 500 ms. The transform doubles its value.
The sink logs the result. All four services run continuously until you stop them.

## 1. Start and verify

From the repository root:

```sh
scripts/demo.sh start
```

The first run builds the images and may take several minutes. It generates two
distinct 256-bit local credentials, stores them in the ignored
`.graphx/demo.env` file with mode `0600`, and reuses them on later starts. The
command waits for telemetry and then samples the live counters twice. A healthy
run ends with five `PASS` lines, including both TCP edges connected and both
message counters advancing. It also prints the control token to paste into the
browser. Run `scripts/demo.sh token` to retrieve that token later.

Open <http://localhost:8080> or <http://127.0.0.1:8080>. Both documented
loopback origins support live WebSocket updates. The top status bar should say
**Traffic flowing**.
The application view shows generator → transform → sink. Each edge displays a
non-zero message rate. Select either edge to see sent/received messages and
bytes, five-second message/byte rates, mean and p95 latency, errors/drops,
backpressure, and recent trace IDs.
The **Network path** view shows the portable demo's Docker bridge path.
Node cards show measured process CPU; this lightweight demo commonly uses less
than 1%, so the console retains two decimal places for those readings.

If port 8080 is in use, select another loopback port and open the URL printed by
the command:

```sh
GRAPHX_PUBLISHED_HTTP_PORT=18080 scripts/demo.sh start
```

### Forwarded browser port versus Docker published port

There are two different port-mapping cases. If Docker itself should publish
host port 18080 to the telemetry container's port 8080, use:

```sh
GRAPHX_PUBLISHED_HTTP_PORT=18080 scripts/demo.sh start
```

Open <http://localhost:18080>. Compose uses the published port when constructing
the default WebSocket origin allowlist.

If GraphX remains published on port 8080 on a remote Linux host and SSH, an IDE,
or another forwarding tool maps browser port 18080 to that remote port, the
browser origin contains `18080`. Start GraphX on Linux with that exact
browser-visible origin allowed:

```sh
GRAPHX_ALLOWED_ORIGINS="http://localhost:18080,http://127.0.0.1:18080" \
  scripts/demo.sh start
```

For an SSH tunnel, run this on the browser machine:

```sh
ssh -N -L 18080:127.0.0.1:8080 user@linux-host
```

Then open <http://localhost:18080>. Do not change
`GRAPHX_PUBLISHED_HTTP_PORT` for this case unless the remote end of the tunnel
is changed too. `GRAPHX_ALLOWED_ORIGINS` must match the browser URL exactly,
including scheme and port; use the externally visible HTTPS origin when a TLS
reverse proxy terminates the browser connection. Supply a comma-separated list
when more than one exact origin is needed. Restart the demo after changing the
allowlist so the telemetry container receives the new environment.

Authenticated source control is enabled automatically for the guided demo.
Paste the token printed by `start` (or by the following command) into the
console's **Control token** field:

```sh
scripts/demo.sh token
```

**Pause source** stops the generator after any in-flight envelope drains; **Resume** starts it
again without resetting sequence numbers and remains available as a recovery
action after a collector restart. The controls remain disabled when
the browser token field is empty or no live runtime is connected.
The server returns an explicit error for invalid credentials. Reset only clears
accumulated counters and does not stop traffic. Fault injection remains a native
network-lab operation.

Advanced deployments can override the generated values by exporting distinct
`GRAPHX_CONTROL_TOKEN` and `GRAPHX_TELEMETRY_SHARED_SECRET` values before
`start`; the demo records those active values in its protected local file so
`scripts/demo.sh token` remains accurate in a later shell. The file is never
committed and the browser keeps the control token only in memory.

For an observation-protected console, also export a third distinct value with
`GRAPHX_OBSERVATION_TOKEN="$(openssl rand -hex 32)"` before startup and enter it
in the console's **Observation token** field. Keep the exports in the shell when
running `scripts/demo.sh verify` so its metrics request is authenticated.

Correlated application-frame capture and durable SQLite history are enabled by
default for this guided demo. Selecting an edge exposes its available PCAPNG
download, and **History** shows retained metadata. Demo defaults bound each node
capture to 64 MiB or 100,000 packets and bound history to one day, 50,000
records, or a 64 MiB main database. Disable either feature when needed:

```sh
scripts/demo.sh start --no-capture
scripts/demo.sh start --no-history
scripts/demo.sh start --no-capture --no-history
```

## 2. Observe actual values

Follow all process output:

```sh
scripts/demo.sh logs
```

Use `Ctrl-C` to stop following logs; this does not stop the containers. Healthy
sink output looks like this:

```text
sink seq=41 value=82 trace=trace-41
sink seq=42 value=84 trace=trace-42
```

The sequence increases by one and `value` is twice the sequence. For a compact
container and sink summary, run:

```sh
scripts/demo.sh status
```

You can rerun the end-to-end traffic check at any time:

```sh
scripts/demo.sh verify
```

Raw telemetry is also available for diagnosis:

```sh
curl http://localhost:8080/api/health
curl http://localhost:8080/api/topology
curl http://localhost:8080/metrics
```

## 3. Stop cleanly

```sh
scripts/demo.sh stop
```

This removes the standard demo's containers and private `graphx` bridge. It
retains the capture/history volumes and `.graphx/demo.env` credentials for the
next start. It does not touch external networks created by the independent
network laboratories.

## If verification fails

Run `scripts/demo.sh status`, then inspect the recent output without following it:

```sh
docker compose logs --tail=100 generator transform sink telemetry
```

Common causes:

| Symptom | Meaning and next action |
|---|---|
| Port 8080 is already allocated, or the browser shows a repository directory | Another HTTP process owns port 8080. Stop it, or run `GRAPHX_PUBLISHED_HTTP_PORT=18080 scripts/demo.sh start` and open the printed URL. |
| A service repeatedly exits | Read that service's logs; stale local images can be rebuilt with `docker compose build --no-cache`. |
| Counts change only after a refresh | The initial HTTP snapshot works but the WebSocket is disconnected. If the browser uses a forwarded port, hostname, or proxy URL, add that exact URL to `GRAPHX_ALLOWED_ORIGINS` and restart the demo; see [Forwarded browser port versus Docker published port](#forwarded-browser-port-versus-docker-published-port). |
| Console says connecting | Verify `curl http://localhost:8080/api/health`, then reload the page. |
| Console connects but waits for samples | Run `scripts/demo.sh verify`; inspect generator and transform logs for connection errors. |
| Counters move but sink output is absent | Inspect the `transformed` edge and sink logs; the verify command reports this edge separately. |
| Pause cannot be clicked | Run `scripts/demo.sh token`, paste the result into **Control token**, and wait for a live runtime. |
| Pause says invalid token | Run `scripts/demo.sh token` again and replace the browser value; externally supplied credentials require a restart after changing them. |
| Fault cannot be clicked | Expected: native Linux fault injection is available in the network labs. |

## Standard demo versus network laboratories

The standard demo is portable on Docker Engine and OrbStack and is the
canonical system smoke test. The examples under `examples/macvlan`,
`examples/ipvlan-l2`, `examples/ipvlan-l3`, and `examples/mixed-network` are
separate, privileged infrastructure laboratories. Their externally created
networks and Compose projects are intentionally not part of this startup path.
On macOS, those system-OVS laboratories run inside the dedicated Lima guest.

For exhaustive developer acceptance after this user-level walkthrough, see
[`test-procedure.md`](test-procedure.md).
For a task-oriented guide to the console, credentials, and captures across all
examples, see [`graphical-examples-guide.md`](graphical-examples-guide.md).
