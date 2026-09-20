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

## Build and inspect

From a clean checkout, build the host CLI first:

```sh
cmake --preset dev
cmake --build --preset dev --target graphx-cli -j 4
```

On macOS, prepare images using the ready OrbStack engine:

```sh
python3 scripts/release/image_release.py build --with-vita --no-cache --allow-dirty \
  --platform linux/arm64 --output outputs/four-radio-vita/images
build/dev/graphx example plan four-radio-vita --target lima --json
```

A successful build ends with `Verified shared image release and derived catalog`.
The VITA smoke checks deliberately launch without configuration or capabilities:
radio, processor and detector must reject missing configuration, and the recorder
must reject missing `NET_RAW`. These expected refusals are reported as `PASS`;
an unexpected exit or diagnostic fails verification. The subsequent `plan` command
prints JSON and does not start the graph.

Choose an absent image output directory. Omit `--allow-dirty` for a clean release
candidate. Normal VITA builds disable qualification hooks; `--qualification-hooks`
is reserved for the private P3/P4 fault matrix. The example workflow rejects those
hook-enabled images. `--with-vita` adds the reusable VITA role and derives an
exactly pinned catalog from verified image bytes. Source catalog images are type
templates, not runnable release evidence.

## Run in the GraphX Lima guest

After explicit authorization, stage the verified image directory on the guest
under `/var/lib/graphx`. Recheck VM identity using the repository Lima tools. Use
that guest-local image path with the standard CLI from the Mac:

```sh
build/dev/graphx example up four-radio-vita --target lima --allow-privileged \
  --images /var/lib/graphx/images/four-radio-vita
build/dev/graphx example status four-radio-vita --target lima --allow-privileged
build/dev/graphx example logs four-radio-vita --target lima --allow-privileged --node detector
build/dev/graphx example open four-radio-vita --target lima --allow-privileged
build/dev/graphx example down four-radio-vita --target lima --allow-privileged
```

The same example workflow runs on authorized native Linux with `--target native-linux`.
No source Compose file or private launcher is needed. Explicit image selection
avoids a second build. Without `--images`, preparation builds the required roles
through the same shared release builder. Use a fresh verified image set for each
verification run and fresh containers for every case.

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
build/dev/graphx example scenario four-radio-vita --target lima --allow-privileged \
  --action iq-loss-jitter --operation plan
build/dev/graphx example scenario four-radio-vita --target lima --allow-privileged \
  --action iq-loss-jitter --operation run
build/dev/graphx example scenario four-radio-vita --target lima --allow-privileged \
  --action iq-loss-jitter --operation clear
```

The action affects inbound IQ from all four radios and inbound control replies;
TCP control remains authenticated and may retransmit. Variable delay may reorder
packets. Nothing applies this fault at ordinary startup. For a failed application,
inspect `status` and `logs`, then use explicit whole-graph recovery:

```sh
build/dev/graphx example down four-radio-vita --target lima --allow-privileged
build/dev/graphx example up four-radio-vita --target lima --allow-privileged \
  --images /var/lib/graphx/images/four-radio-vita
```

Use the same `--instance` on every command when selecting a named instance.
Changing source or artifact selections requires a new instance or explicit
`--restart`. Never restart an individual container behind the ownership ledger.
The recorder discards mirrored frames and cannot backpressure forwarding; capture
is best effort and can lose frames at overload or shutdown. Infrastructure capture
retains separate bounded PCAPNG evidence. No final-packet drain is promised.

In the authenticated console, inspect Application and Network path views, select
nodes to open logs, and read `detection stream=... rf_hz=...` results with their time
and validity fields. Raw edges without packet observations show unavailable metrics;
they do not establish zero throughput or a failed authenticated radio connection.
Use processor/radio logs for control state and independent packet evidence for rates.
Consult [integrated verification](../../design/four-radio-vita/verification.md)
for measured rates, startup timing, browser coverage and current limitations.
