# Four-radio VITA graph

Four independent SoapySDR virtual radios send VITA IQ to a processor/controller.
The processor emits power spectra to a detector. A separate passive recorder
receives/discards mirrored Ethernet frames; it does not archive packets.
All nine application connections traverse the graph's owned OVS bridge at MTU
9000. Management connectivity is separate and the browser remains authenticated.

The graph selects available startup with a five-second readiness window. Failed
applications do not restart automatically. Recover by stopping and starting the
whole graph. Each radio has its own stream identity, control/data ports and fixed
signal frequency. The 2048-point FFT produces 8836-byte spectrum datagrams; IQ
payloads are at most 4128 bytes. Control is mutually authenticated using credentials
staged by the existing GraphX mechanism.

## Build and run

Follow the [top-level build README](../../README.md#build-all-examples) to build
all applications, container images and QEMU artifacts. Its
[four-radio CMake workflow](../../README.md#prepare-and-run-an-example) covers
preparation, startup, inspection and cleanup on Lima or native Linux. Environment
prerequisites, artifact overrides and cache settings are maintained there.

The authored subnet is `10.79.0.0/24`. The console binds guest loopback port 8080;
the standard Lima configuration forwards it to Mac loopback port 18080. Select an
isolated environment without overlapping routes or port use. Optional diagnostic
capture is authored separately and bounded to two 4 MiB PCAPNG files; it continues
independently of the recorder. Runtime state and retained evidence stay in Linux.
Never prune global resources to recover a failed graph.

P5 evidence and current limits are recorded in
[verification](../../design/four-radio-vita/p5-verification.md). P6 sustained and
browser acceptance, physical radios, live retuning, automatic restart and
persistent recorder storage are outside this example's initial scope.

## FFT settings, failures and recovery

Each radio defaults to 1,000,000 complex samples/s, 100 MHz tuning, 800 kHz ideal
bandwidth, zero dB gain and 262,144-sample bursts. The example uses N=2048,
rectangular windows and no overlap: 488.28125 Hz bins. The processor accepts
power-of-two FFT sizes 64–2048, rectangular or Hann windows (`window: 0` or `1`),
and overlap 0/50/75%. Per-stream bin-width numerator/denominator must select an
exact supported FFT size at the configured sample rate. Sample rates are integer
1,000–2,000,000 samples/s. Change the authored parameters before whole-graph startup;
there is no live retuning loop.

`available` startup allows healthy applications to run if another application
cannot become ready. Failed applications remain stopped. Logs distinguish available,
stale and unavailable data; process readiness alone does not establish throughput.
Missing IQ is zero-padded by the processor with explicit validity/gap metadata;
a short final burst packet by itself does not imply missing samples.

After an authorized run is active, this explicit scenario applies 20 seconds of
2% loss and 8±6 ms delay at the processor's owned data attachment:

```sh
cmake --preset dev -DGRAPHX_EXAMPLE_SCENARIO=iq-loss-jitter
cmake --build --preset dev --target four-radio-vita-scenario-plan
cmake --build --preset dev --target four-radio-vita-scenario-run
cmake --build --preset dev --target four-radio-vita-scenario-clear
```

The action affects inbound IQ from all four radios and inbound control replies;
TCP control remains authenticated and may retransmit. Variable delay may reorder
packets. Nothing applies this fault at ordinary startup. For a failed application,
inspect `status` and `logs`, then use explicit whole-graph recovery:

```sh
cmake --build --preset dev --target four-radio-vita-down
cmake --build --preset dev --target four-radio-vita-up
```

Set `GRAPHX_EXAMPLE_INSTANCE` consistently when selecting a named instance.
Changing source or artifact selections requires a new instance or explicit
`--restart`. Never restart an individual container behind the ownership ledger.
The recorder discards mirrored frames and cannot backpressure forwarding; capture
is best effort and can lose frames at overload or shutdown. Infrastructure capture
retains separate bounded PCAPNG evidence. No final-packet drain is promised.

In the authenticated console, inspect Application and Network path views, select
nodes to open logs, and read `detection stream=... rf_hz=...` results with their time
and validity fields. The four IQ edges and spectrum edge publish authenticated
cumulative send/receive packet and payload-byte totals once per second. Application
rates use successive reports; a missed report does not lose the cumulative count.
Rates expire after five seconds without reports. Payload bytes exclude UDP/IP and
Ethernet headers. Latency, unreported drops/errors, CPU utilization and raw control
edge traffic remain unavailable; they must not be inferred from heartbeats. Recorder
mirror counters remain in its logs and are not added to the source data edges.
Use processor/radio logs for control state and independent packet evidence for
network-level rates.
Consult [integrated verification](../../design/four-radio-vita/verification.md)
for measured rates, startup timing, browser coverage and current limitations.
