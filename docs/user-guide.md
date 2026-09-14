# GraphX user guide

GraphX 1.1.0 is a framework for controlled processing and network laboratories.
An authored graph describes typed application instances, connections, execution
placement, observation and optional OVS infrastructure. The CLI builds or verifies
the required artifacts and starts the graph through an identity-checked lifecycle.
The browser shows live traffic and offers only the controls granted to its operator.

Start with [Run an example](../examples/quick-start.md). Use the
[documentation index](README.md) to find the detailed references and the
[example matrix](../examples/README.md) to check a graph's supported targets.

## Contents

**Getting started and everyday use**

- [Installation and prerequisites](#installation-and-prerequisites)
- [Choose and operate an example](#choose-and-operate-an-example)
- [Authenticate and control](#authenticate-and-control)
- [Author a graph](#author-a-graph)
- [Configure networking](#configure-networking)
- [Configure telemetry](#configure-telemetry)
- [Configure PCAP capture](#configure-pcap-capture)
- [Compile inspectable artifacts](#compile-inspectable-artifacts)
- [Stop, retain and recover](#stop-retain-and-recover)
- [Limits and further reading](#limits-and-further-reading)

**Detailed reference**

- [Example selection](#example-selection)
- [Console reference](#console-reference)
- [CLI reference](#cli-reference)
- [Configuration reference](#configuration-reference)
- [Execution administration](#execution-administration)
- [Platform administration](#platform-administration)
- [Observability reference](#observability-reference)
- [History reference](#history-reference)
- [Capture reference](#capture-reference)
- [Scenario actions](#scenario-actions)
- [QEMU laboratory](#qemu-laboratory)
- [Release administration](#release-administration)
- [Manual acceptance checks](#manual-acceptance-checks)
- [Troubleshooting](#troubleshooting)
- [Glossary](#glossary)

## Installation and prerequisites

Run the commands in this guide from a full GraphX repository checkout. The example
workflow requires the checkout even when using an installed `graphx` binary;
`--source /absolute/path/to/checkout` selects it explicitly. Lima source transfer
currently requires a full `.git` directory rather than a linked Git worktree.

| Environment | Requirements and intended use |
|---|---|
| Development host | Git, Python 3, CMake 3.25 or newer, Ninja, a C++20 compiler, OpenSSL 3 development files and Node.js 24.x with npm. Dependency preparation needs network access unless the required inputs are already cached. |
| Native macOS | Native-process examples such as shared memory, UDP unicast/multicast and application capture. Install the compiler through Apple's developer tools and the other prerequisites through your preferred package manager. |
| OrbStack on macOS | Running OrbStack Docker engine, Docker CLI, Compose v2 and Buildx for portable container examples. Select `docker context use orbstack` before these examples. It is not the managed OVS backend. |
| Native Linux | A local Docker engine with Compose/Buildx for containers; native examples need a matching native/platform release. Privileged OVS examples additionally need system OVS, iproute2, namespaces, nftables and the capture/network tools used by their selected scenario. |
| GraphX Lima guest | Apple Silicon host with Lima installed. The checked-in VM provisions the Linux toolchain and privileged laboratory dependencies. Use [Lima setup](../infrastructure/lima/README.md); `example up` does not provision the VM implicitly. |
| Managed QEMU | Verified guest artifacts and combined native/platform/guest installation, QEMU TCG and the dedicated Linux `graphx-qemu` account at UID/GID 65532. See [guest prerequisites](../guests/README.md). A TAP lifecycle check alone does not prove guest execution. |

These are tool requirements, not a claim that every operating-system distribution
or CPU combination has been qualified. See [verification scope](documentation-verification.md).

For Apple Silicon with Homebrew Node 24, select that runtime before building:

```sh
export PATH=/opt/homebrew/opt/node@24/bin:$PATH
```

On other hosts, put your installed Node 24 on PATH. Then build the development CLI:

```sh
cmake --preset dev
cmake --build --preset dev
export PATH="$PWD/build/dev:$PATH"
graphx --version
graphx example --help
```

Expect `graphx 1.1.0` and the example command reference. The build does not start a
graph. `graphx env doctor` checks Docker/Compose as well as the CLI; it is useful
for container workflows, but its Docker requirement is not a native-only prerequisite.
Verified release installation is covered in [execution](#execution-administration-native-installation-and-execution).

## Choose and operate an example

After selecting the environment, use the [quick start](../examples/quick-start.md)
for the complete first-run sequence. The portable sample pipeline sends every
500 ms and runs until stopped. Its OVS variant adds a visible switch and three
owned container veth attachments.

| Step | Command from the repository root | Expected result |
|---|---|---|
| Discover | `graphx example list` | Example names and accepted validation targets; no workloads start. |
| Inspect | `graphx example plan sample-pipeline` | Target, required artifact kinds, node IDs and network paths. |
| Prepare only | `graphx example prepare sample-pipeline --control generator:pause,resume --control collector:reset` | Builds/verifies artifacts and writes a fresh compilation; does not start the graph. Builds can run temporary image smoke containers. |
| Start | `graphx example up sample-pipeline --control generator:pause,resume --control collector:reset` | Ready graph and automatically authenticated console. |
| Reopen | `graphx example open sample-pipeline` | Authenticated console for the same running graph. |
| Inspect ownership | `graphx example status sample-pipeline` | Recorded processes and resource identities. Use the console for application traffic health. |
| Follow logs | `graphx example logs sample-pipeline --node sink --follow` | Bounded recent output followed by new output. Ctrl-C ends log following, not the graph. |
| Restart | `graphx example up sample-pipeline --restart` | Stops the owned run and restarts it, recompiling when the source or selection changed. |
| Stop | `graphx example down sample-pipeline` | Owned runtime resources removed; retained data described below. |

For OVS examples, replace the name with `sample-pipeline/ovs` and include
`--allow-privileged` on runtime commands. Run only with explicit authorization on
native Linux or through the GraphX Lima guest. On macOS, start that environment
explicitly with `graphx env up` first. Standard console ports are host 8080 for
portable examples and host 18080 forwarding guest 8080 for Lima.

The startup command returns after readiness and release. It does not supervise or
automatically repair a failed application. Repeating `up` on an active, unchanged
instance returns its current information. Source/selection changes require
`--restart` or another `--instance`; a separate instance still needs nonconflicting
console ports. Do not run two examples on the same console port.

## Authenticate and control

Interactive `up` and `open` establish a browser session using the current staged
credentials. Refreshing retains access. The login code lasts one minute; the
session lasts up to eight hours and is revalidated against its underlying
credentials. Platform replacement invalidates its in-memory sessions. Reopen with
`example open` when access expires. No token entry is needed in the usual workflow.

Observation access does not imply control. Grant source pause/resume explicitly;
`collector:reset` separately authorizes clearing live collected counters. Reset
does not erase SQLite history or restart applications. Pause first if counters
should remain at zero. Multiple operator credentials require `--operator REF` to
select control access when opening; otherwise the session is observation-only.

Use `--json` or `--no-open` for scripts and headless shells. They suppress browser
opening and return credentials; protect that output. `example tokens NAME` retrieves
current tokens for manual use under **Manual authentication**. Explicit bearer
credentials take precedence over a cookie session. See [CLI authentication](#cli-reference-browser-authentication)
for the session and security contract.

## Author a graph

Names identify instances, not compiled-in roles. Types from the pinned catalog
declare ports, connection cardinality, executable contracts and supported encodings.
The C++ loader resolves shared endpoint settings and rejects incompatible bindings.

This complete finite example can be saved as `examples/my-pipeline/graphx.yml`;
create the directory first. Its catalog path is relative to that file:

```yaml
version: 3
catalog: ../../config/catalog/lock.json
graph: {id: my-pipeline}
nodes:
  source: {type: sample.source, execution: {kind: container}}
  mapper: {type: sample.transform, execution: {kind: container}}
  receiver: {type: sample.sink, execution: {kind: container}}
connections:
  samples: {from: source.samples, to: mapper.samples, transport: tcp}
  results: {from: mapper.transformed, to: receiver.transformed, transport: tcp}
```

The sample type default is 20 messages. For continuous processing, set
`parameters: {max_messages: 0}` on all three sample nodes, as in the maintained
[sample pipeline](../examples/sample-pipeline/graphx.yml). Follow the catalog's
parameter contract rather than adding arbitrary executable arguments.

Validate and inspect the saved graph without an engine or runtime mutation:

```sh
graphx validate examples/my-pipeline/graphx.yml --target orbstack
graphx inspect examples/my-pipeline/graphx.yml --target orbstack
graphx config normalize examples/my-pipeline/graphx.yml --target orbstack
```

Use `native-linux` instead of `orbstack` for this container graph on Linux.
Validation's default target is `native-linux` even on macOS. This differs from
`example` commands, which choose placement from the host and graph. Validation
success is a capability check, not proof of a runnable release or successful startup.
The [configuration reference](#configuration-reference) covers networks, credentials,
platform policy, bounds and resolved application bindings.

## Configure networking

A logical connection describes application communication. Its `edge_paths` entry
can additionally describe the network resources carrying it. Portable container
examples use Compose management connectivity; they do not implicitly create OVS.
Use the OVS sample when a managed switch should appear in **Network path**.

System OVS on Linux realizes Ethernet, MACVLAN and IPVLAN semantic profiles.
Containers attach through owned veth pairs; QEMU attaches through an owned TAP;
router interfaces run in namespaces. Compose owns processes and management bridges.
Physical NICs and external switches are not adopted automatically. See
[network infrastructure](GraphX_Architecture.md#network-ownership-reference) and [scenario actions](#scenario-actions)
for ordered policy, manual routes and bounded faults.

## Configure telemetry

The platform receives authenticated runtime events and serves live topology,
metrics, health and optional exporters. **Application** shows logical edges;
**Network path** shows the declared infrastructure hops. **History** queries
retained observations. Lack of telemetry must not be interpreted as a fabricated
healthy node. Use [the console guide](#console-reference) to interpret the views.

The source catalog enables bounded SQLite history by default. OTLP, Prometheus
and Grafana require their declared configuration and credentials. See
[observability](#observability-reference), [history](#history-reference) and the corresponding
[variants](../examples/README.md). A live service and a ready graph are different:
`/api/ready` checks platform readiness; `/api/graph/ready` checks graph evidence.

## Configure PCAP capture

Application capture records application bytes without packet-capture privileges.
GraphX envelopes use PCAPNG USER0; raw SDR application records use USER1. OVS
capture records mirrored Ethernet traffic and requires the privileged Linux
lifecycle. Enable the appropriate provider in the graph before preparation.
A disabled provider is displayed as unavailable rather than collecting implicitly.

Download available files from **Capture** or use the
[Wireshark integration](../wireshark/README.md). See [capture](#capture-reference) for bounds,
formats and safe paths. Archive desired captures before restart: per-node files
are bounded and may be replaced.

## Compile inspectable artifacts

The example CLI handles compilation for ordinary demos. For architecture review
or integration work, compile explicitly into an absent output directory. From the
repository root on any development host:

```sh
COMPILE_PARENT=$(python3 -c 'import pathlib,tempfile; print(pathlib.Path(tempfile.mkdtemp(prefix="graphx-compile-")).resolve())')
graphx compile examples/sample-pipeline/graphx.yml \
  --target native-linux --source-root "$(pwd -P)" \
  --credential-root "$COMPILE_PARENT/credentials" --output "$COMPILE_PARENT/sample"
```

This writes `compile-manifest.json`, `resolved.json`, per-node bindings and
applicable execution/platform/network plans. It does not provision credentials or
start processes. Source catalog pins remain unverified until matched to real
release artifacts. Use the release-specific catalog when compiling for execution;
see [release packaging](#release-administration-shared-images-and-catalog-pins).
Never edit compiled files to fix a graph: their hashes are verified at startup.

## Stop, retain and recover

Use `example down` before switching examples or modifying runtime resources.
It removes only identity-matched resources. History, selected capture evidence,
bounded logs and shared artifact caches are retained; credential volumes are
removed. `--restart` may create a new graph identity and preserve the old history
separately. `graphx env down` stops Lima while retaining its disk.

History deletion is an explicit advanced operation, never part of ordinary stop;
see [platform lifecycle](#platform-administration-platform-lifecycle-and-access) for its ownership
checks and irreversible effects. Deleting a VM also destroys its retained data.
Do not prune Docker resources or edit ownership ledgers to overcome a mismatch.
Use [troubleshooting](#troubleshooting) and the same original launch reference
for recovery. Avoid changing runtime artifacts while an operation holds the graph lock.

## Limits and further reading

Native and container applications cannot share one authored graph. Physical SDR
startup remains gated until its uplink ownership contract exists; the explicitly
selected simulator uses separate laboratory trust. The managed guest contract uses
TCG; do not infer KVM support or broadcast/multicast guest behavior from its unicast
traffic tests. GraphX provides bounded laboratory workflows, not high availability
or an automatic migration service for stored schemas.

See [glossary](#glossary), [CLI reference](#cli-reference),
[architecture](GraphX_Architecture.md) and [verification scope](documentation-verification.md).

## Example selection

Use the [CLI quick start](../examples/quick-start.md) first. Every demo uses
`graphx example up NAME`, followed by `open`, `status`, `logs` or `down` with
the same name. Interactive startup opens an authenticated console. Explicit
`tokens`, `--json` and `--no-open` support manual or scripted access.

| Behavior | Startup command |
|---|---|
| Continuous portable pipeline | `graphx example up sample-pipeline --control generator:pause,resume --control collector:reset` |
| Continuous OVS pipeline | `graphx example up sample-pipeline/ovs --allow-privileged --control generator:pause,resume --control collector:reset` |
| IPVLAN routing | `graphx example up ipvlan-l3 --allow-privileged` |
| Route and policy diagnostics | `graphx example up static-route-policy --allow-privileged` |
| Simulated SDR | `graphx example up sdr-node/simulated` |
| Explicit laboratory SDR | `graphx example up sdr-node/external --allow-privileged --laboratory laboratory-radio` |
| QEMU guest | `graphx example up qemu-node/tap --allow-privileged` |

On macOS, the CLI selects OrbStack for portable containers and the identity-matched
GraphX Lima VM for privileged labs. Use `graphx env up` explicitly to start that VM.
Preparation, compilation and credential staging happen through the shared workflow;
physical-device startup remains gated. Follow the [complete example matrix](../examples/README.md)
for supported targets and [CLI reference](#cli-reference) for artifact overrides.

Stop a demo before starting another with the same console port. Select scenario
actions explicitly with `graphx example scenario NAME --action ID` and include
`--allow-privileged` for network labs. See [manual checks](#manual-acceptance-checks)
and [test procedure](test-procedure.md) for verification requirements.

<a id="example-selection-select-by-learning-objective"></a>

### Select by learning objective

| Objective | Example family | What to inspect |
|---|---|---|
| Understand continuous graph traffic | `sample-pipeline`, then `sample-pipeline/ovs` | Logical edges versus a declared OVS switch path. |
| Compare process transports | `shared-memory`, `udp-unicast`, `udp-multicast` | Bindings, message delivery and local runtime logs. |
| Observe broadcast delivery | `udp-broadcast` | Container receiver behavior and graph telemetry. |
| Capture application bytes | `capture` | Bounded application PCAPNG and console downloads. |
| Compare network profiles | `macvlan`, `ipvlan-l2`, `ipvlan-l3`, `mixed-network` | OVS endpoints, namespace routing and isolation. |
| Exercise policy and faults | `static-route-policy`, `network-observability` | Declared route/fault actions, diagnostic evidence and capture. |
| Work with raw SDR traffic | `sdr-node/simulated`, explicitly selected external laboratory | Raw bytes, TLS credentials, application versus Ethernet capture. |
| Boot a guest | `qemu-node/tap`, `variants/mixed-container-qemu-sdr` | Actual guest readiness, TCG, TAP, QMP and capture. |
| Configure platform features | `variants/history`, `observability`, `control`, `credential-rotation`, `secure-otlp`, `otlp-mtls` | Storage, exporter and credential contracts; external inputs may be required. |
| Vary instance identity and topology | `variants/renamed-multi-source`, `variants/multi-radio` | Type reuse with distinct node/credential identities. |

The [complete matrix](../examples/README.md) lists every authored input and validation
target. A supported target is not proof that the selected artifact set can execute
it. Use the [user guide](#installation-and-prerequisites) for environment
requirements and the [verification report](documentation-verification.md) for evidence limits.

## Console reference

The portable pipeline and simulated SDR example expose the GraphX browser console. After a
`graphx example up NAME` reports readiness, its authenticated console opens automatically. Verify topology, node and edge
status, rates, latency, SLO state, runtime evidence, capture entries, and history.

Container graphs run with Docker Engine on Linux or OrbStack on macOS. The explicit
SDR laboratory and QEMU/OVS graphs also have the default platform console, with
packet evidence governed by their declared observation contract. Physical SDR
startup remains gated until an uplink ownership contract is implemented.

After resizing the browser or rotating a mobile display, use the topology's
**Fit view** control to bring all nodes back into view. The canvas does not
automatically refit an existing layout when its container changes size.

The QEMU TAP and OVS network labs are primarily command-line demonstrations. Use `graphx example status NAME --allow-privileged` for resource status; packet captures can be opened through
Wireshark extcap or exported with the GraphX CLI.

For commands and environment requirements, use [`demo-guide.md`](#example-selection) and
[`test-procedure.md`](test-procedure.md).

<a id="console-reference-read-the-console"></a>

### Read the console

| View or control | Meaning |
|---|---|
| Application | Typed nodes and logical connections. Click an edge for counters, latency, message identity and declared network hops. |
| Network path | Infrastructure relationships from the compiled graph plus available runtime evidence. A portable example has no implicit OVS switch. |
| History | Retained observations. A history failure is distinct from a live telemetry failure. |
| Capture | Available bounded files and their application/raw/Ethernet format; unavailable means the configured provider has no accessible file. |
| Pause / Resume | Authorized source or runtime control. Read the acknowledgement status rather than assuming a pending request completed. |
| Reset counters | Clears collector counters. Requires `collector:reset`; pause first to keep counters at zero. History remains. |
| Manual authentication | Advanced bearer-token entry. Normally the CLI-established session supplies access automatically. |

Reopen with `graphx example open NAME`; add `--allow-privileged` for an authorized
OVS/guest example. Refresh retains the session. Use [troubleshooting](#troubleshooting)
for expired login, missing traffic or an unauthorized control. A **LIVE** connection
means the console is receiving platform data, not that every application is healthy.

### Node logs and QEMU serial

Re-prepare and restart an existing graph with the updated artifacts before using
node consoles; changing the checkout does not update a running platform image.
Click an application node in the graph to open its Console panel. **Logs** shows
recent output and refreshes automatically. Pause scrolling to inspect output,
search the loaded text, or download the retained bytes. Reading requires the
same observation credentials as the graph; automatic browser login supplies them.

Native processes and namespace applications expose their merged stdout/stderr.
Containers expose bounded Docker output. QEMU nodes show guest boot output from
`ttyS0`, with QEMU process errors in a separate expandable section. An external
device has no logs unless a supported log source is provided. Unavailable or stale
sources are shown explicitly, rather than inferred from network traffic.

Each snapshot retains at most 64 KiB per source. It is a recent-output window,
not a complete archive; older output, rotation and time spent disconnected can
leave gaps. Snapshots show graph generation and runtime identity. Logs remain on
the host after stop, but the stopped platform cannot serve them. The next startup
replaces the retained console directory after ownership checks. Existing native
and Docker log retention remains separate from this console window.

For a QEMU node, select **Serial** to view its separate interactive `ttyS1` port.
Use **Acquire keyboard** to become its single writer and **Release keyboard** to
return to observation. Other viewers remain read-only. Provision permission when
starting the graph, for example:

```sh
graphx example up qemu-node/tap --allow-privileged --control qemu-node:serial
```

This command starts a privileged graph and requires the documented Linux/Lima
prerequisites. A serial grant permits guest console input; pause/resume grants do
not. It uses the existing operator credential and automatic browser session.

**The guest image must provide its own login service on ttyS1.** GraphX does not
create a guest account, generate a password or enable a shell. The maintained
images do not promise an interactive login. Boot diagnostics remain on ttyS0.
A connected, blank Serial view can therefore be correct for an image without a
service on ttyS1. No guest password is the same thing as a GraphX control token.

The terminal is 80 columns by 24 rows, with bounded scrollback. Input is limited to
4 KiB per batch. Writer leases expire after ten seconds without authenticated
renewal and last at most fifteen minutes, bounded further by browser-session expiry.
Closing the view releases the writer when possible; lease expiry handles lost
connections. Stopping a viewer does not stop the guest. Node restarts and credential
revocation invalidate access. Terminal input is not recorded in control audit logs;
guest output can still contain sensitive information.

## CLI reference

`graphx example` is the common workspace interface for the authored examples.
It calls the authoritative C++ loader/compiler, verified release builders and
existing `graphx run` ownership lifecycle. Python 3 and a source checkout are
required for preparation; no additional configuration parser is introduced.
`graphx config authored FILE` exports the loader-validated authored object as JSON.

| Command | Behavior |
|---|---|
| `graphx example list` | List authored examples |
| `graphx example plan NAME` | Report target, nodes, paths and required artifacts without mutation |
| `graphx example prepare NAME` | Prepare artifacts and compile without starting |
| `graphx example up NAME` | Prepare if needed, start and open an authenticated console |
| `graphx example open NAME` | Reopen an authenticated console without restarting |
| `graphx example status NAME` | Verify ownership and report resource status without tokens |
| `graphx example tokens NAME` | Verify ownership and retrieve current staged tokens |
| `graphx example logs NAME --node NODE --follow` | Follow application logs |
| `graphx example down NAME` | Stop through the common ownership lifecycle |
| `graphx example scenario NAME --action ID` | Run an explicit declared scenario action |
| `graphx env doctor` | Check CLI, Docker/Compose and macOS Lima inventory |
| `graphx env up` / `down` | Explicitly start/stop the GraphX Lima environment on macOS |
| `graphx verify PROFILE` | Run an existing verification profile |
| `graphx release TOOL …` | Run the existing release tool with its original arguments |

Release tools are `native`, `images`, `platform`, `guests`, `install`, `verify` and
`publish`. Release publication remains an explicit command. For example,
`graphx release images verify DIR` verifies an existing image candidate.

Common options:

- `--source CHECKOUT`: source repository, otherwise discovered from the working directory.
- `--target TARGET`: explicit supported placement; default is native Linux on Linux,
  or OrbStack/native macOS/Lima according to the authored execution model on macOS.
- `--images DIR`, `--release DIR`, `--catalog DIR`, `--external DIR`: existing artifact and external
  credential inputs. Paths are guest-local when targeting Lima from macOS.
- `--control NODE:pause,resume` or `--control QEMU_NODE:serial`: explicit operator grant on a working copy; repeat
  for additional nodes. Add `--control collector:reset` to authorize Reset counters.
  Reset clears collected metrics; it does not restart applications or erase history.
- `--allow-privileged`: required for OVS/guest work and its ownership checks.
- `--laboratory ID`: explicit pre-start laboratory substitution.
- `--restart`: stop the current owned run before restarting or replacing its compilation.
- `--instance NAME`: separate launch reference and graph identity.
- `--workspace DIR`: local workspace root; defaults to `outputs/examples` on macOS
  and `/var/lib/graphx/examples` on Linux. Lima runtime state always stays in the guest.
- `--no-open`: skip browser opening and return the URL and tokens for manual use.
- `--operator REF`: select one existing control credential for browser login when multiple are declared; otherwise login is observation-only.
- `--json`: suppress browser opening and return a machine-readable result; progress is sent to stderr.
- `--operation plan|run|status|clear`: operation for an explicit scenario action.

Working copies contain the selected authored graph, including any control grant.
A private `current.json` stores only generation and artifact references, with no
tokens. It is not a deployment configuration. The compiler's manifest and the
runner's ownership ledger remain authoritative. Launch and artifact locks prevent
concurrent conflicting preparation. Source changes require an explicit restart.
Each compilation generation gets a distinct graph identity, preserving prior history.

Lima dispatch checks the existing VM's repository identity, copies a source
snapshot without macOS metadata, builds a guest-local CLI and uses the same
workflow there. It never provisions a VM as a side effect of `example up`.
The host keeps a reference to the snapshot so status and cleanup use the original
runner after source changes. The standard console forward is guest 8080 to host
18080; other ports require an explicit environment forwarding configuration.

`up --no-open`, `up --json`, and `tokens` return the observation credential and any enabled control
credential. Interactive `up` and `open` instead establish a browser session and omit
the tokens from terminal output. With multiple authored operator grants, `control_token` is a mapping
from credential references to tokens. Disabled control returns null. The CLI
never grants access merely because a node supports control. Explicit generated
operator tokens are staged through the normal external credential provider.

See [quick start](../examples/quick-start.md), [execution](#execution-administration) and
[release process](#release-administration) for prerequisites and lower-level contracts.

For QEMU, `logs` defaults to the platform telemetry log. Raw QEMU boot logs remain
in the runner-owned guest directory; `--node` selects container or native process
logs. A supplied combined guest installation needs its matching `--catalog DIR`.

<a id="cli-reference-browser-authentication"></a>

### Browser authentication

The local CLI authenticates to the loopback console with the current staged tokens
and obtains a single-use login code valid for 60 seconds. It opens that code in
a URL fragment, which the page removes before exchanging it for an HttpOnly,
SameSite=Strict cookie. Tokens are never embedded in page assets or returned by
a public bootstrap endpoint. HTTPS sessions also set Secure.

Sessions are bound to one graph, console origin and existing credential scope,
expire after eight hours, and are revalidated against the underlying credentials.
Revocation and platform replacement invalidate them. Refresh keeps the session;
use `graphx example open NAME` to authenticate again. Cookie-authenticated control
requests require a session CSRF header and an allowed Origin for mutations.
HTTP observations and WebSocket streams use the same session. Manual bearer
authentication remains available under **Manual authentication**, and retains
precedence when supplied explicitly.

On macOS, browser opening and login happen on the host even when the example runs
in Lima. Automated jobs should use `--json` or `--no-open`. If browser opening
fails, the started example remains running; retry `open` or retrieve `tokens`.
No control permissions are added by browser login.

<a id="cli-reference-lower-level-reference"></a>

### Lower-level reference

These interfaces are for inspecting or integrating the compiled workflow. Run
from the repository root with the same installed/development CLI. `FILE` is an
authored graph unless the row explicitly calls for normalized JSON. `DIR` and
other uppercase arguments are placeholders for canonical absolute paths.

| Command | Input and effect |
|---|---|
| `graphx validate FILE --target TARGET` | Checks the authored graph and catalog; no runtime mutation. |
| `graphx inspect FILE --target TARGET` | Prints the resolved graph. |
| `graphx config authored FILE` | Exports the loader-validated authored object as JSON, preserving authored intent for workspace orchestration. |
| `graphx config normalize FILE --target TARGET` | Emits normalized contract 2; no credential material or runtime provisioning. |
| `graphx node-settings --node ID --config FILE` | Validates and prints a selected node's bindings from normalized JSON. |
| `graphx compile FILE --output DIR --source-root DIR --credential-root DIR --target TARGET` | Writes a new immutable compilation; output must be absent. `--catalog-root DIR` selects the trusted catalog. |
| `graphx run plan --output DIR --state-root DIR` | Verifies a compilation and prints its resource plan without creating runtime resources. |
| `graphx run up\|status\|down --output DIR --state-root DIR` | Common owned lifecycle. Startup needs applicable `--images`, `--release`, `--credentials` and/or `--external` inputs. Privileged paths require explicit Linux authorization and `--allow-privileged`. |
| `graphx scenario plan\|run\|status\|clear --action ID --output DIR --state-root DIR` | Selects one declared action through the common ledger; see [scenario rules](#scenario-actions). |

For actual command invocations, choose one verb from a multi-verb reference row;
do not type the vertical bars. The workspace CLI avoids requiring these paths for
every demo. `graphx infra` is disabled; `platform-lock` is an internal ownership
helper rather than a deployment shortcut. The platform's staging, rotation and
history-removal maintenance interfaces are documented in [platform](#platform-administration).

<a id="cli-reference-automation-and-failures"></a>

### Automation and failures

A nonzero exit means the selected operation did not complete; inspect stderr and
the graph's status rather than inferring that all resources were removed. Interrupted
startup can leave recoverable state. `up` can also succeed with `browser: unavailable`
when the graph started but browser login failed; retry `open` without restarting.
JSON operation output is distinct from progress and diagnostic stderr.

`prepare` can build artifacts and run isolated release smoke checks but does not
start the graph. `up` starts an absent run or reuses an unchanged active one.
`--restart` explicitly stops before restarting/recompiling. Preserve external
credential and artifact selections when changing a working copy; existing selections
are remembered when omitted. A new `--instance` gives a separate launch reference,
not automatic port isolation. Do not run parallel examples on the same console port.

## Configuration reference

`graphx.yml` with `version: 3` is the only authored format. The C++ loader resolves
catalog type instances and connections into normalized JSON contract version 2.
Version 2 authored files and normalized version 1 are deliberately rejected.

The implemented commands are `validate`, `inspect`, `config normalize`, and
`node-settings --node ID --config FILE` for resolved node validation, and `compile`.
`inspect` prints the resolved JSON. These commands read bounded files and do not
start processes, provision credentials, create infrastructure, or inspect a Docker
engine. Compilation writes inspectable files only. `run plan` verifies a compilation
and prints its network resource plan without runtime operations. `run up|status|down`
provides [owned execution](#execution-administration); OVS requires explicit local Linux
privileged opt-in. Managed QEMU guests additionally require verified guest artifacts
and the dedicated Linux account; see [guest releases](../guests/README.md). `infra` is a disabled legacy entry point; use the common `run` lifecycle and
explicit [scenario actions](#scenario-actions). Physical-device startup remains gated
pending an uplink ownership contract.

```sh
build/dev/graphx validate examples/sample-pipeline/graphx.yml --target orbstack
build/dev/graphx config normalize examples/sample-pipeline/graphx.yml --target lima > resolved.json
```

`--target` selects `native-linux`, `native-macos`, `orbstack`, or `lima`.
The validation default is `native-linux`, independent of the host. Native macOS
accepts native processes, while OrbStack accepts container placement. Managed OVS,
namespaces and QEMU require Linux or Lima. Target validation is a pure capability
check; success does not constitute execution evidence.

<a id="configuration-reference-authored-model"></a>

### Authored model

- `version`: integer 3.
- `catalog`: relative path from the graph file to a SHA-256 catalog lock.
- `graph`: one stable `id`.
- `nodes`: map from instance ID to catalog `type`, `execution`, optional
  `parameters`, credential references and observation declarations.
- `connections`: map from connection ID to `from`, `to`, transport and optional
  settings, explicit attachments, feedback declaration and security references.
- `network`: logical OVS networks, switches, routers, attachments, captures and
  connection paths. Router interfaces expand into attachments. Optional
  `router.interfaces[].port` selects a switch port and preserves its VLAN.
- `portable_network`: explicit subnet/address map when portable automatic
  allocation is insufficient.
- `platform`: bounded capture, history, console, control, OTLP and extension
  overrides. SQLite history defaults enabled.
- `credentials`: provider/identity/member descriptors only. The loader never opens these paths.
- `scenario`: bounded declarations of actions, never executed by normalization.

The [authored schema](../config/schema/graphx.schema.json) is closed at every
structured object. Catalog [type definitions](../config/schema/node-type.schema.json)
provide port direction, wire schema/encoding, transports, connection cardinality,
feedback support, defaults, execution kinds and application identity. The catalog
also owns control capability; telemetry does not infer it from sample node IDs.
The source catalog contains illustrative image pins. Container execution requires
a release catalog derived from verified OCI archives; native execution requires
a verified installed release. Mixed native/container applications are rejected
with `E_EXECUTION_MIX`; use separate graphs.

The default catalog trust root in a development build is `config/catalog` in its
source checkout. Use `--catalog-root DIR` for a relocated or installed catalog;
installations include it under `share/graphx/catalog`. The authored relative lock
path must resolve inside that explicit root. Absolute paths, escapes, symlink
components, duplicate lock entries and digest mismatches fail closed. Locks contain
at most 1,024 files, each at most 1 MiB, with a total at most 16 MiB.

Input is one YAML/JSON document of at most 1 MiB and 64 nesting levels. Explicit
YAML tags, anchors, aliases, merge keys, duplicate keys, unknown fields and wrong
scalar types are rejected. Node and connection limits are 1,024 and 4,096.
Credentials and diagnostics contain references and field paths, never credential
contents. There is no `--set` or environment override interpreter;
`GRAPHX_OVERRIDES` does not change the authored input.

<a id="configuration-reference-resolved-contract"></a>

### Resolved contract

The [normalized schema](../config/schema/normalized-graph.schema.json) closes all
nested structures, including node bindings, transport settings, resource models
and platform policy. The root contains `contract_version: 2`, `graph_version: 3`,
`graph_id`, target, catalog/input digests, ordered node and connection arrays,
network, portable network, platform, credential IDs and execution placeholders.
Both endpoints receive the same resolved transport settings. IDs and object maps
use lexicographic ASCII ordering. Logical OVS names expand to bounded deterministic
physical names; no runtime ownership token is fabricated by normalization.

Telemetry accepts only this resolved contract and validates it with the same
schema. Its existing standalone HTTP, history and capture modules remain testable
with explicitly supplied runtime wiring. The runner stages graph credentials and
control grants before releasing applications. `${GX_STATE}` and other execution
placeholders are resolved by the execution adapter, not the configuration loader.
Normalization does not write artifacts. The compiler serializes this resolved
model into the node files and applicable plans described below.

<a id="configuration-reference-application-bindings"></a>

### Application bindings

The C++ `load_node_settings` API reads one resolved node object, using the node
shape from the normalized schema and the release's embedded catalog type
contracts. It rejects mismatched or absent instance identity, unknown type or
revision, wrong ports/roles/schema/encoding, peer cardinality violations, malformed
parameters, invalid resolved endpoints and unbounded startup settings before
opening application resources. It does not interpret authored graphs.

`graphx node-settings --node ID --config FILE` validates this object and prints
canonical JSON. The SDR Python programs use this C++ command through `GRAPHX_CLI`
(default: the installed `graphx` executable), then consume its validated output.
There is no second authored parser or endpoint environment fallback.

The sample and UDP executables require `--node ID --config FILE` plus
`--release-file FILE --release-token TOKEN`. SDR service scripts accept the same
arguments. The token is a fresh 32–128 byte invocation identity supplied by the
caller. After binding listeners/local resources, each process flushes
`ready node=ID` to stdout. It then waits up to `startup.max_wait_ms` for a bounded,
regular, non-symlink release file containing exactly that token, with no newline.
Connectors and sample traffic stay held until release. SIGINT/SIGTERM interrupts
the wait; C++ TCP retry and shared-memory connection setup also accept cancellation.
TCP/UDP senders bind the resolved source address. The execution adapter owns
safe directory staging, ownership checks, readiness collection and release-file
creation by atomic rename; this application interface does not implement graph orchestration.

Ports such as `samples` are application type contracts, not connection IDs. The
source, transform and sink consume their resolved parameters and actual node/edge
identities. A transform rejects the wrong incoming wire type, malformed integer
samples and multiplication overflow. Application capture uses the resolved node
identity and capture limits. Null telemetry credential references disable network
telemetry; a non-null reference requires an explicitly supplied runtime secret
through the existing secret/file interface. The runner stages credentials and maps TLS references to owned files. Raw SDR
control uses those explicit TLS files and resolved endpoints/server names.

Node files are emitted by deterministic compilation and consumed through the
shared node-settings contract. The native binding tests extract
node objects into temporary fixtures and supply their own barrier. They do not
establish compiled graph, container, OVS or guest execution acceptance.

<a id="configuration-reference-deterministic-compilation"></a>

### Deterministic compilation

`graphx compile FILE --output DIR --source-root DIR --credential-root DIR`
accepts the same `--target` and `--catalog-root` options as normalization. All
roots are explicit and may not contain symlinks or parent traversal; the output's
parent must exist. The source and credential roots are protected boundaries,
not instructions to build sources or read secrets. Output must be outside them,
the authored input directory and the catalog root. Use canonical absolute paths
(for example `/private/tmp` on macOS rather than its `/tmp` symlink).

The compiler emits a manifest with graph/catalog/input digests and hashes of every
other artifact. Node JSON files match normalized node objects exactly. Plans cover
the platform, credentials, fixed Compose/native launch templates and applicable
OVS/capture/guest/scenario work. Compose is emitted as canonical JSON in
`compose.yaml`, a valid YAML 1.2 representation. No compiler step invokes runtime
tools, builds artifacts, provisions credentials or probes infrastructure.

The manifest's `execution_available` and execution plan's `executable` indicate
whether the graph uses supported native, container, namespace or QEMU adapters. This is a capability
flag, not release verification. `artifact_identities_verified` remains false at
compilation; `graphx run` verifies actual installed files or OCI archive identities
before execution. Physical-device startup remains gated pending its uplink contract.

Publication uses private staging, fsync and exclusive atomic rename. An existing
output is never replaced, even with a matching manifest. Use a fresh output
name; `--replace` is refused until runtime ownership can establish inactivity.
See [P3 verification](../design/graph-generation/p3-verification.md) for the
artifact matrix, reproducibility evidence and remaining execution boundaries.

Catalog node-type revisions are positive integers from 1 through 2147483647.
Release image packaging increments the affected container type revisions and
repins the catalog lock together; see the [release process](#release-administration).

Scenario action fields and references are validated by the C++ loader. See
[explicit scenario execution](#scenario-actions) for selection, laboratory compilation,
rotation bounds and recovery behavior.

The `sample.source`, `sample.transform`, and `sample.sink` types accept
`parameters.max_messages: 0` to run until explicitly stopped. A positive value
limits processing to that many messages (the type default is 20). Use the same
limit on every node in a sample pipeline. An unexpected upstream disconnect is
an error, including in continuous mode. The sample-pipeline example explicitly
selects continuous mode; its source emits every 500 ms by default.

## Execution administration

`graphx run up` consumes an existing compilation and a verified release. It checks
artifact hashes, target, executable/image identity, resource paths and listener
ports before launching applications. Native and container applications must use
separate graphs. OVS and namespace execution require explicit local Linux privileged
opt-in and have S07–S12 Lima acceptance evidence. QEMU uses verified guest artifacts; scenario actions require explicit selection.

Startup is finite: stage credentials, start the platform, wait for listeners,
credentials and SQLite history, start applications held at their local readiness
barrier, verify every process, and publish the ownership token to release traffic.
The command then returns. It does not supervise, restart or repair applications.
`status` inspects recorded identities; `down` stops only owned resources.

<a id="execution-administration-native-installation-and-execution"></a>

### Native installation and execution

Commands in this reference run from the repository root with the prerequisites in
the [user guide](#installation-and-prerequisites). Uppercase shell
variables are operator-supplied paths or release metadata, not built-in defaults:
`GX_OUTPUT` is the immutable compilation, `GX_STATE` its separate state parent,
`GX_CREDENTIALS` private staging, and `GX_RELEASE` a verified installation.
`NATIVE_CANDIDATE` and `PLATFORM_CANDIDATE` identify matching archive sets;
`COMMIT` and `SOURCE_DATE_EPOCH` must match their manifests. Do not use empty or
invented values. Ordinary users should let [the example CLI](#cli-reference) manage them.

Build and verify the native and platform companion archives as described in
[release-process.md](#release-administration). Install archives from the same commit,
version, platform and source epoch into an absent directory:

```sh
python3 scripts/release/install_release.py --native "$NATIVE_CANDIDATE" \
  --companion "$PLATFORM_CANDIDATE" --output "$GX_RELEASE" \
  --commit "$COMMIT" --version 1.1.0 --epoch "$SOURCE_DATE_EPOCH"
```

Local dirty companions require explicit `--allow-dirty`; they are local test
candidates, not published releases. Installation writes an exact file-hash/mode
receipt. Startup verifies that receipt, including the bundled Node executable.
Use canonical absolute paths without symlink components for all execution roots.
The graph state directory and credential root must not overlap compiled output,
an installed release or an image release directory.

Compile a native example using the instructions in the [user guide](user-guide.md).
Then supply the installed release, compiled output, state parent and private
credential staging root explicitly:

```sh
"$GX_RELEASE/bin/graphx" run up --output "$GX_OUTPUT" --state-root "$GX_STATE" \
  --release "$GX_RELEASE" --credentials "$GX_CREDENTIALS"
"$GX_RELEASE/bin/graphx" run status --output "$GX_OUTPUT" --state-root "$GX_STATE"
"$GX_RELEASE/bin/graphx" run down --output "$GX_OUTPUT" --state-root "$GX_STATE"
```

Native state is below `GX_STATE/GRAPH_ID`. Readiness has a bounded wait; SIGINT or
SIGTERM before release triggers identity-checked rollback. Native processes use
PID, kernel start time, executable path and executable hash checks. A mismatch
refuses cleanup. Managed application output retains at most 2 MiB per process,
plus one previous log; capture and SQLite quotas are independent. This limits
managed output streams, not arbitrary writes by third-party native code. Native
application allocation limits are not a kernel RSS limit.

<a id="execution-administration-container-execution"></a>

### Container execution

Confirm the selected engine with `docker info` and `docker compose version`.
On macOS select OrbStack. Compile with the verified image release's catalog,
using the release catalog workflow in [release-process.md](#release-administration).
Then run:

```sh
build/dev/graphx run up --output "$GX_OUTPUT" --state-root "$GX_STATE" \
  --images "$GRAPHX_IMAGE_RELEASE"
build/dev/graphx run status --output "$GX_OUTPUT" --state-root "$GX_STATE"
build/dev/graphx run down --output "$GX_OUTPUT" --state-root "$GX_STATE"
```

External credential providers additionally require `--external DIRECTORY`, laid
out as `REFERENCE/MEMBER`, containing private regular files. The adapter stages
copies inside an owned volume; it does not alter the originals. Secrets travel
through stdin, never command arguments or environment values. Node-specific
credential subpaths are mounted read-only.

The adapter verifies OCI archive hashes and compiled release pins, loads missing
images, and uses their immutable local image configuration IDs in a runtime
Compose projection. The original compilation remains unchanged. Fixed pinned
Prometheus/Grafana extensions may require a registry pull. Containers run as
65532 with all capabilities dropped, read-only roots, bounded memory/PIDs/logs,
and no restart policy. Compose manages processes and bridge management networks.
No privileged operation is part of portable execution.

Containers, networks and volumes use the existing ownership store. Creation
intents are saved before Docker mutations; observed immutable IDs and owner/graph
labels are checked before cleanup. Container names alone never authorize removal.
`down` retains owned history and application capture volumes and bounded logs;
credential volumes are removed. Image cache entries are shared and retained.
Capture files remain available after stop; restarting may replace the bounded
per-node capture file. Archive desired evidence before restarting.

<a id="execution-administration-compiled-ovs-execution"></a>

### Compiled OVS execution

Inspect the verified compilation without contacting an engine or creating state:

```sh
build/dev/graphx run plan --output "$GX_OUTPUT" --state-root "$GX_STATE"
```

On separately authorized native Linux or the existing GraphX Lima guest, compile
for `native-linux` or `lima` using verified release pins. Pass `--allow-privileged`
to `run up`, `status` and `down`; startup also needs `--images`. Namespace diagnostic
applications need a matching verified native `--release` containing
`graphx-diagnostic`. Native application placement cannot own OVS endpoints.

The runner requires a local root-owned Docker Unix socket, confirms container
ownership and local cgroup membership, prepares data interfaces and management
ACLs while containers wait before exec, then applies the ordinary listener/release
barrier. Router policies preserve authored order and manual routes remain deferred.
Infrastructure and process identities share one graph lock and ownership record.
Cleanup validates the complete inventory before stopping processes, removes owned
infrastructure, then deletes Compose objects. It retains sealed captures and logs.

The platform receives complete read-only PCAPNG snapshots from an owned exporter;
live capture rings remain on Linux under `/var/lib/graphx`. Physical external
uplinks are not implicit. S14 remains gated until its separate uplink contract is
designed. Scenario commands require explicit action selection. Guest boot also requires verified guest artifacts
and the dedicated QEMU account; the privileged flag alone does not satisfy these prerequisites.

See [P7 verification](../design/graph-generation/p7-verification.md) for the
implemented scope, unprivileged checks, guarded acceptance harness and
S07–S12 Lima results. Native Linux OVS is not verified by the Lima run. Network profile wrappers now use the same compiled runner.

<a id="execution-administration-recovery-and-examples"></a>

### Recovery and examples

If readiness fails, inspect the returned error and the graph's ownership file and
logs. Run `down` with the same compilation to finish interrupted cleanup. If an
identity differs, recover the original resource or investigate the mismatch;
do not rewrite identities to make cleanup pass. A missing retained history volume
also requires investigation. Existing unrelated workloads are left untouched.

Use `graphx example up|open|status|logs|down NAME` for the supported user workflow.
The CLI remembers compiled output, state and verified artifact paths. Retained
low-level wrappers require explicit execution roots and call the same adapter;
they are maintenance interfaces, not standalone preparation workflows. There are
no source Compose overlays. See [CLI reference](#cli-reference).

<a id="execution-administration-managed-qemu-guests"></a>

### Managed QEMU guests

Compile S15 or T03 with a [verified guest release catalog](../guests/README.md),
then pass the combined native/guest installation as `--release`. QEMU uses TCG
and UID/GID 65532 (`graphx-qemu`), only its owned TAP, local QMP and a bounded
serial ring. Artifact hashes and architecture are checked before infrastructure
creation. Configuration, credential-generation files and readiness use three
bounded local virtio channels with peer and invocation identity checks.

The same process ledger handles startup, failure, status and cleanup. Runtime
directory identity is validated before stopping any owned resources. Guest graphs
resolve a 180-second application release wait to cover the shared 120-second
guest handshake. Source-catalog placeholder artifacts cannot boot. See
[P8 verification](../design/graph-generation/p8-verification.md) for build/check
evidence and the separately authorized actual-boot gate.

Scenario actions never run during startup. Use the separately selected
[scenario interface](#scenario-actions) for routes, faults, traffic and credential rotation.

## Platform administration

Every compiled graph has one platform configuration. `graphx-platform --config
<output>/platform.json` consumes that file and the sibling `resolved.json` and
`credentials.json`. The C++ resolver remains authoritative; the platform verifies
that its configuration agrees with normalized contract 2. It never normalizes
YAML at startup. `graphx run` stages credentials and starts this platform before releasing native
or portable container applications; see [execution](#execution-administration). OVS and guest infrastructure use the same owned runner with explicit Linux
authorization and their required artifacts. `/api/ready` requires history to be ready (or disabled),
as well as HTTP/UDP listeners and valid credentials.

The native platform companion archive installs `bin/graphx-platform`, pinned Node
24.20.0, telemetry modules, production dependencies and built web assets alongside
the native C++ release. Put that release's `bin` directory on `PATH`. The shared
Dockerfile has `runtime`, `telemetry` and `sdr` targets; telemetry and SDR reuse the
C++ runtime. All run as UID/GID 65532. No graph is baked into an image.

<a id="platform-administration-explicit-provisioning"></a>

### Explicit provisioning

Compile into a fresh directory outside the declared input and credential roots.
Compilation does not create credentials. Set `GX_CREDENTIALS` to an absent private
staging directory whose parent exists, then provision explicitly:

```sh
graphx-platform stage --manifest "$GX_OUTPUT/credentials.json" \
  --destination "$GX_CREDENTIALS" --external "$EXTERNAL_CREDENTIAL_FILES"
```

Omit `--external` when the manifest has no external references. The external
input layout is `REFERENCE/MEMBER`. Each input must be a bounded private regular
file, with no symlinks or hardlinks. Provisioning copies external bytes; it never
creates a replacement external CA or device key. Runtime-generated references
use OS randomness for distinct HMAC/read tokens. Lab-generated references require
OpenSSL and share one ephemeral CA per staging operation, with separate leaf keys,
resolved SAN identities and server/client EKUs. The CA private key is discarded.

Staging creates the root exclusively with mode 0700 and each reference directory
with mode 0500 and files with mode 0400. A failed stage leaves a private incomplete
destination for inspection and explicit cleanup; it cannot be mistaken for a
successfully published bundle. Never put credentials in a graph, image layer,
command argument or environment variable. Environment variables contain paths
and the non-secret ownership identity only.

Run provisioning as the identity that will read the files: the native user for
native applications, UID/GID 65532 in an unprivileged container for Docker. The
engine acceptance test uses a dedicated volume initialized from the image's
owned state directory. Mount only the reference subdirectories listed for each
consumer, not the complete staging root. Privileged Linux guest staging and graph lifecycle orchestration use the common
[execution adapters](#execution-administration).

<a id="platform-administration-platform-lifecycle-and-access"></a>

### Platform lifecycle and access

Set `GX_STATE` to the state directory and `GX_OWNER` to the stable 32–128 character
ownership identity for this instance before starting the platform. Native state
defaults to `~/Library/Application Support/graphx/GRAPH` on macOS or
`$XDG_STATE_HOME/graphx/GRAPH` (fallback `~/.local/state`) on Linux. Managed Linux
container state is `/var/lib/graphx`. Use canonical paths without symlinks.

```sh
graphx-platform --config "$GX_OUTPUT/platform.json"
```

The launcher replaces its process through `graphx platform-lock`, retaining the
common exclusive ownership lock until exit. SQLite records both graph and owner
identity. A second writer fails closed. Stop preserves history. Explicit deletion
requires that same identity and an inactive writer:

```sh
graphx-platform history-remove --config "$GX_OUTPUT/platform.json" --owner "$GX_OWNER"
```

**This permanently deletes retained history.** Perform it only after explicit
authorization and any required backup. Deletion checks all selected history paths before removing only SQLite's database,
WAL and SHM files. It keeps the stable lock file and does not delete captures,
credentials or application state. It is never part of routine stop/down.

The console requires observation authorization for graph reads, including on
loopback. The CLI establishes a [browser session](#cli-reference-browser-authentication)
from the staged `observer/token` and any selected control credential. Manual
bearer authentication remains available. Readiness endpoints expose only bounded health information. Compiled
origins control HTTP/WebSocket access. Each observed GraphX node has its own HMAC;
external nodes have no fabricated GraphX identity. Control uses resolved credential,
action and node grants, existing nonce/replay protection, idempotency and durable
audit. Grant combinations do not widen one another's node/action permissions.
Unconfigured administrative audit permissions remain denied.

Application management bridges remain internal. A separate console bridge serves
loopback HTTP publication and platform OTLP egress; application processes do not
join it. UDP telemetry is not published to the host. Optional Prometheus/Grafana
services use fixed digest pins, loopback ports, bounded temporary storage and
scoped credential mounts. The compiler emits their scrape, alert, datasource and
dashboard configurations. Prometheus reads the observer token from its file;
Grafana reads its external `grafana-admin/password` file. For optional service deployment, check the graph target and execution plan; do
not assume native-process placement supports container-only extensions.

<a id="platform-administration-rotation-and-bounds"></a>

### Rotation and bounds

A stable reference directory survives rotation. Explicit rotation checks provider,
identity and member roles, retains the previous generation for at most 60 seconds,
and publishes generation hashes after atomic member replacement:

```sh
graphx-platform rotate --manifest "$GX_OUTPUT/credentials.json" \
  --destination "$GX_CREDENTIALS" --credential operator --next operator-next --grace 60
```

The operation takes the common exclusive staging lock. Readers reject incomplete,
changed or stale generations. Previous-token acceptance expires on a monotonic
clock; wall-clock expiry limits overlap after a reader restart. Tokens remain
redacted during bounded retirement. C++ and SDR telemetry reread staged HMAC files;
TLS sessions retain their negotiated identity until reconnect. Declared rotation actions run through the explicit `graphx scenario run` interface;
see [scenario execution](#scenario-actions).

History reserves storage for the database, WAL frames and SHM together. It refuses
new writes when checkpointing cannot reclaim storage and reports drops without
stopping live telemetry. Queue count/bytes, pending queries, query deadlines and
shutdown are bounded. A query deadline degrades history and terminates its worker
instead of allowing expired queries to refill an unbounded worker queue. Existing
files that exceed a reduced storage allocation are rejected before service use.

## Observability reference

For the end-to-end configuration and verification workflow, see
[telemetry and observability](#configure-telemetry).

GraphX emits bounded metrics and trace events for nodes, edges, transports, drops,
errors, reconnects, queue pressure, and latency. The telemetry service combines
those events with normalized topology and runtime evidence.

Current endpoints include liveness, readiness, graph readiness, health, SLO,
topology, metrics, history, capture catalog/download, runtime evidence, and
authorized control. Observation and control credentials are separate. Remote
plaintext binding is rejected unless explicitly enabled; TLS and mutual TLS are
available.

OTLP/HTTP export is asynchronous and bounded by queue size, response size, timeout,
retry, and backoff settings. Export failure degrades the relevant status without
blocking graph traffic. Prometheus and Grafana examples consume the same current
metrics surface.

## History reference

The optional history service stores bounded telemetry and control-audit records in a
graph-owned SQLite database. It uses WAL journaling, full synchronization,
transactional batches, retention pruning, record limits, and a configured database
size ceiling.

The worker initializes the current schema for a new database and rejects a database
whose schema version or graph identity does not match the running service. It does
not rewrite an unsupported schema.

`/api/history` and `/api/history/status` require observation authorization through the configured token or an authenticated
browser session.
History degradation does not stop live telemetry; readiness reports the history
failure independently. Back up the database only while the service is stopped or by
using SQLite's online backup facilities.

The compiled platform holds the common writer lock and records owner identity.
Stop preserves the store; explicit owner-checked deletion and aggregate storage
bounds are described in [the platform lifecycle](#platform-administration).

## Capture reference

For the end-to-end configuration and verification workflow, see
[PCAP configuration](#configure-pcap-capture).

Application capture writes framed GraphX envelopes to bounded PCAPNG files using the
USER0 link type. Metadata includes direction, edge, sequence, timestamp, message,
trace, parent, and type information.

Network capture is a separate owned lifecycle. An OVS mirror sends Ethernet frames
to a capture attachment, and a bounded capture process writes rotating files under
an absolute, identity-checked directory. GraphX records the process start time,
interface identity, and directory identity before cleanup.

`tools/graphx-extcap` exposes current captures to Wireshark. The Lua dissector
decodes envelope wire format 2 and can also decode configured UDP ports. Capture
catalog and download APIs validate paths and enforce file and response limits.

<a id="capture-reference-raw-sdr-application-bytes"></a>

### Raw SDR application bytes

SDR Python services capture the bytes they send and receive as bounded PCAPNG
LINKTYPE_USER1 (148). These records are labeled `raw-application`; they contain
no synthesized Ethernet, IP or GraphX envelope. GraphX framed application capture
uses USER0 (147), and network-wire capture uses Ethernet (1). The platform can
list and download each node's separately mounted `NODE/NODE.pcapng` without
recursive traversal or following symlinks. Its UI identifies raw records as
application bytes. Application capture requires no packet-capture capabilities.

## Scenario actions

Compile the authored `scenario.actions` with the graph. Baseline `graphx run up`
never executes them. Inspect or select one action by its declared ID:

```sh
"$GX_RELEASE/bin/graphx" scenario plan --output "$GX_OUTPUT" --state-root "$GX_STATE"
"$GX_RELEASE/bin/graphx" scenario run --action ACTION_ID --output "$GX_OUTPUT" \
  --state-root "$GX_STATE" --release "$GX_RELEASE"
"$GX_RELEASE/bin/graphx" scenario status --action ACTION_ID --output "$GX_OUTPUT" \
  --state-root "$GX_STATE"
```

Networking and QMP actions require an authorized local Linux root invocation with
`--allow-privileged`. On macOS use the existing GraphX Lima guest; these commands do
not provision or select a VM. Credential rotation also supports native applications
and unprivileged Compose on OrbStack. A scenario requires the exact compilation and
a ready graph. Unknown IDs, fields, references and operations fail before mutation.
The CLI verifies the compilation manifest and resolved references, takes the existing
exclusive graph lock, and writes bounded action records into `ownership.yml`.

| Action | Behavior |
|---|---|
| `fault` | Applies netem to an identity-owned data attachment. The existing fault timer clears it at its bounded monotonic deadline; `scenario clear --action ID` explicitly clears an owned fault sooner. It refuses an existing custom qdisc. |
| `route-apply` | Adds an absent, declared manual route in its owned router namespace. It records the observed route identity and never replaces an existing route. |
| `route-clear` | Deletes only the matching route recorded by its declared apply action. `scenario clear` on the apply action has the same ownership check. |
| `traffic` | Runs bounded checks on declared connections. Diagnostic UDP probes carry unique tokens and check receipt or absence. Guest checks cover its resolved unicast TCP/UDP contract, VLAN isolation, nonempty capture and identity-checked QMP pause/resume. |
| `credential-rotate` | Publishes the declared next reference through the common credential-generation implementation, preserving provider, identity and member roles. The overlap is 1–60 seconds and expires across reader restart. No credential values enter plans, logs or command arguments. |
| `external-simulator` | Requires explicit pre-start `compile --laboratory ACTION_ID`; it cannot replace a running physical device. |

`scenario status` reports the recorded action outcome (`not-run`, `pending`,
`complete`, or `cleared`); use `run status` for graph health. A timed fault's action
record remains complete after its timer expires. Clear it before applying it again.
Completed rotations cannot be replayed. Traffic checks may be repeated after success.
Routes can be applied again after their owned clear.

A pending record refuses retry: it may describe an interrupted mutation or failed
verification. Keep that record for inspection, then use the common identity-checked
`run down` and a fresh `run up` to recover the graph before selecting the action
again. A process, route, qdisc, directory or resource identity mismatch fails closed.
Cleanup never recreates a baseline or adopts a same-named external resource. History
and bounded capture evidence remain under the graph's ordinary retention policy.

<a id="scenario-actions-laboratory-selection-and-physical-trust"></a>

### Laboratory selection and physical trust

```sh
"$GX_RELEASE/bin/graphx" compile examples/sdr-node/external/graphx.yml \
  --laboratory laboratory-radio --target lima --catalog-root "$GRAPHX_CATALOG" \
  --source-root "$GRAPHX_SOURCE" --credential-root "$GX_CREDENTIALS" \
  --output "$GX_OUTPUT"
```

This explicit selection resolves the declared radio as `sdr.simulator`, converts its
attachment into an owned container veth, and substitutes the declared laboratory
credentials for the radio and processor. The compilation records the selection in
`laboratory-selection.json`. When the external attachment has no switch, selection requires exactly one owned
switch among attachments on that network; missing or ambiguous bindings fail.
It uses the ordinary shared SDR image and OVS lifecycle;
there is no separate simulator launcher, source manifest or physical uplink.

The selected graph has an isolated owned bridge with no physical-device attachment.
It neither probes nor takes ownership of a real radio. Physical startup stays gated
until the separate uplink ownership contract exists. The original graph and external
credential files remain unchanged; stopping the laboratory removes its staged test
credentials. Returning to physical operation requires a separate compilation of the
original declaration and the future physical-uplink gate. It never silently reuses
laboratory trust for a physical device.

See [P9 verification](../design/graph-generation/p9-verification.md) for actual test
coverage and the remaining privileged acceptance commands.

## QEMU laboratory

`qemu-node/tap` runs a verified x86_64 guest under QEMU TCG with an owned TAP
attached to system OVS. It is a privileged Linux laboratory. On macOS, the
example CLI dispatches to the existing GraphX Lima guest and keeps runtime state
on its disk. It does not forward privileged sockets or use QEMU user networking.

First complete [CLI installation](#installation-and-prerequisites),
[guest prerequisites](../guests/README.md), and, on Apple Silicon,
[Lima setup](../infrastructure/lima/README.md). With explicit authorization for the
privileged laboratory, run from the repository root:

```sh
graphx example plan qemu-node/tap
graphx example up qemu-node/tap --allow-privileged
graphx example open qemu-node/tap --allow-privileged
graphx example status qemu-node/tap --allow-privileged
graphx example logs qemu-node/tap --node platform --allow-privileged
graphx example down qemu-node/tap --allow-privileged
```

Preparation builds or verifies the image, native/platform and guest artifact sets.
Existing `--images`, `--release` and `--catalog` inputs must be compatible; on Lima
these paths are guest-local. Startup checks artifacts and handshake readiness
before releasing application traffic. Source-catalog placeholder artifacts cannot boot.

Inspect guest state, declared VLAN/network paths and available capture in the
console. Select only declared [scenario actions](#scenario-actions) for bounded traffic,
VLAN and QMP checks. No scenario runs merely because the graph starts. The
[example guide](../examples/qemu-node/tap/README.md) names its available actions.

`logs` defaults to platform telemetry for guest graphs; raw QEMU serial output is
retained in the owned guest runtime and is not selected as an application log by
`--node`. Cleanup validates QEMU process and runtime-directory identity before
stopping it and removing its TAP. History and selected bounded evidence remain.
Actual guest boot/application results are distinct from TAP-only lifecycle tests;
TCG and unicast tests do not establish KVM or guest broadcast/multicast support.

## Release administration

Run from a full repository checkout with a built `graphx` on PATH. These are
release-engineering workflows, not prerequisites for using `example up`, which
can prepare a local candidate. Each output directory below must be absent. Builds
may download pinned dependencies; locally stored artifacts do not imply an offline build.
Publication and privileged acceptance require separate explicit authorization.

1. Update `VERSION` and the package versions in `apps/telemetry` and `web`.
2. Run `scripts/verify.sh portable`, then the platform-specific full and privileged
   checks required by the changed surfaces.
3. Build a clean release candidate:

```sh
graphx release native \
  --build-dir build/release \
  --output-dir outputs/release \
  --tag "v$(tr -d '\n' < VERSION)"
```

4. Verify it independently:

```sh
graphx release verify outputs/release --source .
```

The verifier checks archive layout and modes, native package contents, checksums,
image digests, the SPDX SBOM, configuration schemas, and installed consumer use.
Published artifacts must be built from a clean tree and use digest-pinned base
images. Keep signing credentials outside the repository.

<a id="release-administration-shared-images-and-catalog-pins"></a>

### Shared images and catalog pins

The runtime, telemetry and SDR images have one software recipe each:
`Dockerfile` targets `runtime`, `telemetry` and `sdr`. They use
the two fixed catalog templates, `node-v1` and `platform-v1`. No graph generates a
Dockerfile or selects a build context. Runtime packaging has no sample graph or
generator entrypoint. The SDR image installs the three catalog executable names;
it contains application code, not the packet-observation tooling owned by the
infrastructure lifecycle. All three images run as UID/GID 65532.

After checking the selected Docker engine (`orbstack` on macOS), build a locally stored
candidate without publishing or creating a privileged builder:

```sh
graphx release images build \
  --output outputs/shared-images --platform linux/arm64 --no-cache
graphx release images verify outputs/shared-images
```

Use `linux/amd64` for an amd64 candidate. `--allow-dirty` is available for local
implementation verification and explicitly marks the image manifest as a dirty
candidate. Such output is not a publishable release. Every build runs twice;
`--no-cache` forces independent builds. Debian dependencies use the dated,
signature-verified snapshot in `docker/debian.sources`; npm dependencies use the
checked-in locks, and the SDR image installs Python from the pinned Debian snapshot on the shared
runtime image.

The builder verifies Docker's OCI-compatible save output, normalizes layer and
archive timestamps offline, and derives new OCI config and manifest digests from
the resulting bytes. It preserves layer order, ownership, modes, links and xattrs.
The exporter explicitly requests OCI media types and uncompressed layers;
timestamp normalization is independent of Docker image-store behavior. Both classic
config-digest IDs and containerd manifest-digest IDs are checked against verified
image bytes before smoke execution. Both the complete archive bytes and image inventories must repeat.
The finished archives are imported and smoke-tested with no network, a read-only
root filesystem, no capabilities and the declared non-root user. Only temporary
image identities are cleaned up. No graph workload is launched.

`images.json` binds each archive checksum to its OCI manifest digest and actual
installed files. Each image includes a separate SPDX inventory derived from its
installed OS packages, language runtime and Node modules. The independent verifier
rehashes every layer and regenerates that inventory. It rejects embedded authored
graphs, credential directories and PEM private keys; exact published GnuTLS
self-test fixtures are recognized only inside `libgnutls`. This is a bounded
content check, not a claim to detect every possible encoding of a secret.

The generated `catalog/` is a release-specific copy of the authoritative catalog,
with image pins, container type revisions and lock hashes updated together. Release
image builds set `GRAPHX_RELEASE_IMAGE_TYPES=ON` to embed those exact container
type revisions in the authoritative node reader. Ordinary development builds
retain source catalog revisions.
Author a graph with a relative path to that lock and pass its directory as
`--catalog-root` when compiling. The source catalog remains an explicitly
unverified development input. Offline OCI digests do not imply registry
availability; publishing requires preserving or independently verifying the
registry manifest. `graphx run` verifies these offline archives against the
compiled release catalog, then runs an immutable local image-ID projection.
Native execution uses the verified combined installation. Guest execution requires a verified guest artifact set and combined installation.

The release workflow builds, attests, validates SPDX inventories and promotes
all three shared registry images together. It refuses existing version tags and
includes SDR in compensating cleanup. Running the local commands above does not
invoke that publication workflow.

Native releases include a separately verified platform companion archive. Build it
with `graphx release platform --output FRESH_DIRECTORY --epoch EPOCH`.
The builder verifies the reviewed Node archive pins in `scripts/release/node-runtime.json`,
installs locked dependencies, builds web assets and emits deterministic archive
bytes, an SPDX inventory and a file-hash/mode manifest. `--node-archive FILE` permits
an already downloaded archive with the same required checksum. Builds require a
clean worktree; `--allow-dirty` marks a local candidate that the publication
verifier rejects. Install the companion
alongside the corresponding C++ archive; its launcher uses the bundled Node binary.
The release workflow verifies commit/version/platform/epoch for both artifacts
before publication. The lab credential provider also requires OpenSSL.

<a id="release-administration-installing-a-native-execution-release"></a>

### Installing a native execution release

Use `graphx release install` to verify and combine native and platform
companion archives with matching version, commit, platform and epoch. The
exclusive installation includes the file-hash/mode receipt consumed by `graphx run`.
See [execution](#execution-administration) for exact arguments and local candidate policy.

## Manual acceptance checks

Run [`test-procedure.md`](test-procedure.md) first. Then perform the checks relevant
to the changed area. The [everyday workflow](#choose-and-operate-an-example) defines the
commands and terminology these manual observations use.

<a id="manual-acceptance-checks-portable-system"></a>

### Portable system

1. Run `graphx example up sample-pipeline --control generator:pause,resume`.
2. Confirm the automatically opened console shows live metrics and history without token entry; refresh and confirm authentication persists.
3. Send permitted pause/resume commands and confirm the audit record.
4. Run `graphx example down sample-pipeline` and confirm its owned containers are gone.

<a id="manual-acceptance-checks-privileged-network-labs"></a>

### Privileged network labs

1. Select an example with `graphx example plan NAME`; macOS selects Lima for OVS.
2. Run `graphx example up NAME --allow-privileged`, then `graphx example status NAME --allow-privileged`.
3. Confirm OVS ports, addresses, routes, policy, capture, and fault state expected by
   that example.
4. Run `graphx example down NAME --allow-privileged` and confirm only owned resources were removed.

<a id="manual-acceptance-checks-qemu-tap"></a>

### QEMU TAP

1. Run `graphx example up qemu-node/tap --allow-privileged --control qemu-node:serial`.
2. Confirm bidirectional TCP/UDP traffic, VLAN isolation, SPAN capture growth,
   packet history, and QMP runtime evidence.
3. Select the QEMU node in the web console. Confirm Logs shows guest boot output,
   then connect Serial and acquire/release its writer. Serial input requires the
   explicit grant above; a guest login service on ttyS1 must be supplied by the
   selected image. The maintained image does not promise a login prompt.
4. Stop it and confirm TAP, bridge, namespace, QEMU and live ownership resources are
   removed by the owned lifecycle.

## Troubleshooting

Run these commands from the repository root with the development `graphx` on
PATH. Substitute the actual example name. For OVS, namespaces or QEMU, include
`--allow-privileged` only on an explicitly authorized Linux/Lima runtime operation.
The checks below do not authorize broad Docker cleanup, ledger edits or VM replacement.

| Symptom | Check and expected finding | Recovery |
|---|---|---|
| `graphx` is not found | `build/dev/graphx --version` should print the project version. | Build using [installation](#installation-and-prerequisites), then add `build/dev` to PATH. |
| Node version rejected | `node --version` must select major 24 for portable verification and platform preparation. | Select Node 24 on PATH; on Apple Silicon use the quick-start Homebrew path when installed. |
| Docker unavailable or wrong engine | `docker context show`, `docker info`, `docker compose version`; macOS portable examples use OrbStack. | Start/select the intended engine. `env doctor` checks dependencies but does not start Docker. Native-only workflows do not require an engine. |
| Unexpected target rejection | `graphx example plan NAME` and `graphx example list` show supported placement. Direct validation defaults to native Linux even on macOS. | Select an accepted `--target`; do not confuse normalization with actual runtime acceptance. |
| Lima stopped | `limactl list graphx` shows its state. | Use `graphx env up` for explicit environment startup. This can provision the environment when absent; review [Lima setup](../infrastructure/lima/README.md) first. |
| Stale VM configuration | Compare the error with the checked-in VM definition/provisioning inputs and repository location. Host helpers alone do not change the provisioning digest. | Investigate the actual mismatch. Do not overwrite identity markers. Back up required guest data before any separately approved removal/recreation; deletion is not ordinary graph cleanup. |
| Source or selection changed | `up` reports that its prepared generation differs. | Use `graphx example up NAME --restart` for the owned run, or a separate instance with nonconflicting ports. This can generate a new graph identity. |
| Console port in use | `graphx example status NAME` identifies an owned run; inspect the selected host port with your OS tools. | Stop the known owner or author a different console port. Do not kill an unidentified process. |
| Browser did not open | CLI reports the graph is running but browser login is unavailable. | `graphx example open NAME`; headless users can use `tokens` and manual authentication. Browser failure does not mean startup rolled back. |
| Login expired, empty page after graph replacement, or HTTP 401 | `graphx example status NAME` should show the current graph. An old tab may retain an expired session or manual token. | Reopen with `graphx example open NAME`. A login code lasts 60 seconds and can be consumed once. Expand **Manual authentication** only for deliberate bearer access. |
| Login endpoint unavailable with old images | New CLI can start against an older verified artifact set whose platform lacks session support. | Build a current verified image/platform release and select it with `--images` or `--release` during an explicit restart. Do not modify files inside running containers. |
| Pause works but Reset fails | Review the control scope printed by startup; source pause/resume does not authorize collector reset. | Restart with both `--control generator:pause,resume` and `--control collector:reset`. These names apply to the sample; use declared node IDs elsewhere. |
| Control disabled or forbidden | Check the selected operator grant and whether a controllable runtime is connected. Observation-only sessions cannot control. | Reopen with `--operator REF` when multiple operator credentials are declared. A grant must already exist; login does not create permissions. Use the printed loopback origin rather than an alternate hostname. |
| Command pending or timed out | Read the browser command result and `graphx example logs NAME --node NODE`. HTTP acceptance is not a runtime acknowledgement. | Diagnose runtime readiness/control identity. Do not assume retry succeeded or manually bypass command authentication. |
| Pipeline stopped after a small number of messages | Inspect each sample node's `parameters.max_messages`; default is 20. | Set all sample nodes to 0 for continuous operation, then restart. The maintained sample pipeline already does this. |
| No live metrics | Check **LIVE**, node readiness and `logs`; a platform can be live while the graph is not ready. | Fix startup, credentials or connection failures. Use History for retained records; do not interpret missing telemetry as zero traffic. |
| Network path has no OVS switch | `graphx example plan NAME` shows declared paths. Portable Compose connectivity does not create OVS. | Use `sample-pipeline/ovs` with authorized Linux/Lima execution, or author explicit network attachments and paths. |
| Capture unavailable | Check the graph's capture provider and the console catalog. Application capture and mirrored Ethernet are different sources. | Enable the intended provider and restart. OVS capture requires privileged Linux execution; no packet capture is enabled implicitly. |
| History degraded or refused | Read **History** status and platform logs for quota, schema or ownership failures. | Preserve evidence and stop through `down` before backup or repair. Do not delete database/WAL files while the writer is active. |
| Artifact hash or type revision mismatch | Check the selected verified release and its derived catalog. Compiled output must be unchanged. | Prepare/compile again with compatible release inputs and a fresh output directory. A source placeholder digest is not a real release pin. |
| Ownership mismatch or interrupted cleanup | `status` and retained logs identify the recorded generation and resource. | Use the original reference/compilation with `down`. Investigate a replaced object; never edit IDs, adopt same-named resources or run a broad prune to force success. |
| Scenario is pending | `graphx example scenario NAME --action ID --operation status` reports the recorded action state. | Preserve the failure evidence and use owned down/up recovery before rerunning. A completed timed fault may require explicit clear before reuse. |
| Physical SDR will not start | The external graph declares a device but has no accepted physical-uplink ownership path. | Physical startup remains gated. For a laboratory run explicitly select `--laboratory laboratory-radio`; it uses simulator credentials, not physical trust. |
| QEMU startup fails before traffic | Check verified guest/catalog/combined release inputs, Linux account, QMP/serial evidence and handshake diagnostics. | Follow [managed guest prerequisites](../guests/README.md). Do not substitute TAP-only tests for a boot result or assume KVM is supported. |

<a id="troubleshooting-gather-a-useful-support-report"></a>

### Gather a useful support report

Record the GraphX version, host architecture, selected target, exact command with
secrets removed, authored graph, error code, ownership/status summary and relevant
bounded logs. State whether a failure occurred during preparation, startup,
application traffic, a scenario action or cleanup. Distinguish host macOS, OrbStack,
Lima guest Linux and QEMU guest architectures.

Do not attach `example tokens` output, staged credentials, browser cookies, login
codes or private external credential files. Read [support](../SUPPORT.md) and
[security reporting](../SECURITY.md) before sharing evidence.

## Glossary

| Term | Meaning in GraphX |
|---|---|
| Authored graph | Version 3 `graphx.yml` expressing intent, node instances and optional infrastructure/platform policy. |
| Catalog | Locked type, schema, template, target and guest definitions used by the authoritative loader. |
| Release pin | Digest or identity tied to actual verified artifact bytes. A source-catalog illustrative pin does not establish runnable artifacts. |
| Node type / instance | Reusable catalog contract / a named occurrence of that type in a graph. |
| Port / binding | Named type endpoint / resolved connection and settings for a particular instance. |
| Logical edge | Application connection between ports, independent of whether its infrastructure path is displayed. |
| Normalized graph | Contract version 2 JSON produced by the C++ loader and consumed downstream. It contains credential references, not secrets. |
| Compilation | Deterministic projection into immutable, hash-inventoried runtime artifacts; it does not start a workload. |
| Execution target | Capability selection: native Linux, native macOS, OrbStack or Lima. Validation acceptance is separate from execution evidence. |
| Management network | Platform/telemetry/control connectivity managed with process deployment; not an implicit OVS data path. |
| Data plane | Application traffic carried by selected transports and, when declared, owned OVS infrastructure. |
| OVS | System Open vSwitch on Linux, the managed switch backend. |
| MACVLAN / IPVLAN profile | Semantic network behavior realized through OVS and Linux policy, not a Docker network driver selection. |
| Veth / TAP | Paired Linux network interfaces used for container/namespace attachment / virtual Ethernet interface used by QEMU. |
| Namespace | Isolated Linux networking context used for routers and diagnostic applications. |
| Release barrier | Local readiness/release coordination holding connectors until prerequisites are satisfied; unrelated to publishing software. |
| Owner identity | Runtime identity binding a resource to a graph and configuration. A resource name alone proves no ownership. |
| Observation credential | Authorization to read graph observations, history and available captures. |
| Control grant | Explicit credential/action/target scope for runtime control; not implied by observation access. |
| Handoff / session | One-minute single-use browser login code / bounded authenticated browser access established from current credentials. |
| Collector | Telemetry service target for Reset counters; not an application node reset command. |
| History | Retained SQLite observations and audit records; separate from live counters and bounded application logs. |
| Application / wire capture | Bytes recorded by an application / mirrored Ethernet packets recorded by the Linux capture lifecycle. |
| QMP / TCG | QEMU's management protocol / software CPU emulation used by the managed guest contract. |
| Scenario action | Explicit bounded operation such as a fault, route change, traffic check or credential rotation. |
| Laboratory selection | Pre-start substitution of a declared external device with an explicitly authored simulator and test trust. |
| Dirty candidate | Locally built artifact set from uncommitted source, explicitly marked and ineligible as a published clean release. |

See [configuration](#configuration-reference), [architecture](GraphX_Architecture.md) and
[CLI reference](#cli-reference) for the corresponding contracts.
