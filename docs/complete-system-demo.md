# Run the complete GraphX demo

This is the recommended first run. It starts the complete portable system: three
C++ node containers, their private Docker bridge, the telemetry collector, and
the browser console. You do **not** need to build C++, install npm packages, run
`graphx infra`, or start the advanced macvlan/ipvlan laboratories first.

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

The first run builds the images and may take several minutes. The command waits
for telemetry and then samples the live counters twice. A healthy run ends with
five `PASS` lines, including both TCP edges connected and both message counters
advancing.

Open <http://localhost:8080>. The top status bar should say **Traffic flowing**.
The application view shows generator → transform → sink. Each edge displays a
non-zero message rate. Select either edge to see sent/received messages and
bytes, five-second message/byte rates, mean and p95 latency, errors/drops,
backpressure, and recent trace IDs.
The **Network path** view shows the portable demo's Docker bridge path.
Node cards show measured process CPU; this lightweight demo commonly uses less
than 1%, so the console retains two decimal places for those readings.

Pause and fault injection are visibly disabled in this demo because the runtime
control plane is not implemented. Reset clears accumulated counters; it does not
stop traffic or disconnect the live TCP paths.

To include correlated application-frame capture, start with
`GRAPHX_CAPTURE_ENABLED=true scripts/demo.sh start`. Selecting an edge then
exposes its available PCAPNG download. Capture remains disabled by default to
avoid unbounded files during an ordinary long-running demo.

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

This removes the standard demo's containers and private `graphx` bridge. It does
not touch external networks created by the independent network laboratories.

## If verification fails

Run `scripts/demo.sh status`, then inspect the recent output without following it:

```sh
docker compose logs --tail=100 generator transform sink telemetry
```

Common causes:

| Symptom | Meaning and next action |
|---|---|
| Port 8080 is already allocated | Stop the other service using the port, then rerun `scripts/demo.sh start`. |
| A service repeatedly exits | Read that service's logs; stale local images can be rebuilt with `docker compose build --no-cache`. |
| Console says connecting | Verify `curl http://localhost:8080/api/health`, then reload the page. |
| Console connects but waits for samples | Run `scripts/demo.sh verify`; inspect generator and transform logs for connection errors. |
| Counters move but sink output is absent | Inspect the `transformed` edge and sink logs; the verify command reports this edge separately. |
| Pause or fault cannot be clicked | Expected: these require a future authenticated control plane. Native Linux fault injection is available in the network labs. |

## Standard demo versus network laboratories

The standard demo is portable on Docker Engine and Docker Desktop and is the
canonical system smoke test. The examples under `examples/macvlan`,
`examples/ipvlan-l2`, `examples/ipvlan-l3`, and `examples/mixed-network` are
separate, privileged infrastructure laboratories. Their externally created
networks and Compose projects are intentionally not part of this startup path.
Native macvlan and ipvlan behavior requires a Linux host; the macOS mixed-network
profile is a userspace-OVS simulation.

For exhaustive developer acceptance after this user-level walkthrough, see
[`test-procedure.md`](test-procedure.md).
