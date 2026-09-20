# Run an example

Run these commands from the GraphX repository root. The CLI prepares verified
artifacts, compiles the graph, stages credentials and starts the common owned
runner. It remembers the output and state paths for subsequent commands.

## Install the development CLI

Install the [development prerequisites](../docs/user-guide.md), Python 3, Node.js
24, Docker and Compose. On Apple Silicon with Homebrew Node 24:

```sh
export PATH=/opt/homebrew/opt/node@24/bin:$PATH
scripts/verify.sh quick
export PATH="$PWD/build/dev:$PATH"
graphx env doctor
graphx example list
```

The bootstrap build is the only step that needs a script before `graphx` exists.
Afterward, use `graphx verify quick` or `graphx verify portable`. An installed CLI
also supports this workflow when run from a checkout, or with `--source CHECKOUT`.

## Start the sample pipeline

```sh
graphx example up sample-pipeline --control generator:pause,resume --control collector:reset
```

The first run builds and verifies shared images; subsequent runs reuse prepared
artifacts. This creates a local development candidate, not a published release.
To reuse your existing verified images, add `--images /absolute/path/to/images`.
On macOS, select the OrbStack Docker context for this portable example.

Startup opens an authenticated browser console and prints its URL and control
grant. Refreshing the page preserves the session. The
sample sends a message every 500 ms and runs until stopped. Control is optional:
omit `--control` to leave it disabled. Existing authored grants are respected.

```sh
graphx example status sample-pipeline
graphx example tokens sample-pipeline
graphx example logs sample-pipeline --node sink --follow
graphx example down sample-pipeline
```

Use `graphx example open sample-pipeline` to reopen the authenticated console.
Sessions last up to eight hours and expire when the platform restarts or their
credentials are revoked. `--no-open` skips browser login; `--json` also suppresses
browser opening. Manual token entry remains under **Manual authentication**.

`up` and `tokens` support `--json`. Progress and subprocess logs go to stderr;
stdout contains the result, including the requested credentials. `status` does
not print tokens. Tokens are retrieved from staged credentials, not stored in
the CLI's launch reference.

## Connect the sample through OVS

On macOS, use the identity-matched GraphX Lima VM. Environment creation/startup is
explicit; see [Lima setup](../infrastructure/lima/README.md).

```sh
graphx env up
graphx example plan sample-pipeline/ovs
graphx example up sample-pipeline/ovs --allow-privileged \
  --control generator:pause,resume --control collector:reset
graphx example tokens sample-pipeline/ovs --allow-privileged
graphx example down sample-pipeline/ovs --allow-privileged
```

The CLI selects Lima on macOS, transfers a source snapshot, builds its Linux CLI
and prepares artifacts on the guest disk. It invokes the same runner through
Lima; privileged sockets are never forwarded. Open the printed URL, normally
**http://127.0.0.1:18080**, and select **Network path**. Native Linux uses port
8080 directly. See the [OVS sample guide](sample-pipeline/ovs/README.md).

## Other examples

```sh
graphx example up sdr-node/simulated
graphx example logs sdr-node/simulated --node sink
graphx example down sdr-node/simulated

graphx example up shared-memory
graphx example down shared-memory

graphx example up ipvlan-l3 --allow-privileged
graphx example down ipvlan-l3 --allow-privileged
```

Native examples require a verified native installation and platform companion;
`prepare` builds and installs them automatically. `--release DIR` reuses an
existing verified installation. QEMU examples additionally prepare verified guest
artifacts; guest builds can take substantially longer. On Lima, `--images`,
`--release`, `--catalog` and `--external` refer to **guest-local absolute paths**.

```sh
graphx example up qemu-node/tap --allow-privileged
graphx example down qemu-node/tap --allow-privileged

graphx example up sdr-node/external --allow-privileged --laboratory laboratory-radio
graphx example down sdr-node/external --allow-privileged
```

Physical SDR startup remains gated. The laboratory selection is explicit.
Examples declaring external credentials need `--external DIR` containing the
referenced files; see [credential staging](../docs/user-guide.md#execution-administration-container-execution).
The CLI generates an operator credential only for an explicit `--control` grant.

Scenario actions remain explicit after startup:

```sh
graphx example scenario network-observability --action source-delay --allow-privileged
graphx example scenario network-observability --action source-delay \
  --operation clear --allow-privileged
```

See the [complete example matrix](README.md) and [CLI reference](../docs/user-guide.md#cli-reference).

## Restart and recovery

Stop one example before starting another using the same console port. `down`
removes owned resources and retains history, logs and shared image caches.

`prepare NAME` performs preparation and compilation without startup. `plan NAME`
shows the target and required resources without building or starting anything.
If source or control selection changes, use `up NAME --restart` to stop the old
owned run and compile a fresh generation. `--instance NAME` selects a separate
launch reference; it does not allocate another console port. Existing credentials
and external paths remain explicit inputs when preparing a new generation.

Before switching a manually launched graph to this interface, stop it with its
original `graphx run down --output DIR --state-root DIR` command (and its
privilege flag where required), so its console port is available.

An interrupted startup keeps its launch reference: use `status` and `down` with
the same example, target and instance. Never manually rewrite ownership files.
A Lima identity mismatch stops automatic dispatch; follow the deliberate recovery
procedure in the Lima guide. The CLI never silently replaces an existing VM.

Reset counters requires `--control collector:reset`. If this example is already
running with pause/resume only, repeat the `up` command above with `--restart`
to apply both grants and open a newly authenticated console.
Reset clears collected metrics; it does not restart the pipeline or erase history.

## Four virtual radios in Lima

The [four-radio VITA example](four-radio-vita/README.md) uses four virtual SoapySDR
radios, an FFT processor/controller, frequency detector and passive recorder.
After explicitly authorizing privileged execution and preparing the dedicated
GraphX Lima guest, use the same example workflow:

```sh
build/dev/graphx example plan four-radio-vita --target lima
build/dev/graphx example up four-radio-vita --target lima --allow-privileged
build/dev/graphx example open four-radio-vita --target lima
build/dev/graphx example logs four-radio-vita --target lima --allow-privileged --node detector
build/dev/graphx example down four-radio-vita --target lima --allow-privileged
```

Preparation selects normal VITA images automatically. An explicit verified guest
`--images` path reuses a selected release. OVS carries application traffic; the
console uses Mac loopback port 18080. Failed applications do not restart themselves;
recovery is an explicit whole-graph down/up. Recorder reception and diagnostic
capture are best effort. See the example's FFT settings and
[integrated verification](../design/four-radio-vita/verification.md) before treating
laboratory measurements as sustained or real-time guarantees.
