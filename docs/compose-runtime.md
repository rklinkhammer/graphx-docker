# Shared Compose runtime

`infrastructure/compose/services.yaml` defines the reusable native, SDR, telemetry,
and normalization services. It owns process hardening, bounded logs, PID limits,
and the no-restart policy. The root demo, simulated/external SDR, broadcast, and
OVS laboratory Compose files extend these services. Their topology-specific
mounts, capabilities, connectivity, and feature overlays remain in their profiles.

`python3 scripts/instance.py` renders and runs explicit-instance container graphs.
It consumes the C++ loader's normalized configuration and resource resolution;
it does not parse YAML or maintain another topology manifest. Generated Compose,
credentials, and container receipts are runtime artifacts. Edit `graphx.yaml`,
then stop the old deployment with its original configuration before starting the
new configuration. Changing configuration or overrides while active fails closed.

## Platform selection

| Workload | Linux | macOS |
|---|---|---|
| Sample pipeline, two-source SDR | Docker Compose v2 | OrbStack Compose v2 |
| Instance OVS sample | Explicit root invocation with system OVS | Run the same launcher inside the dedicated GraphX Lima guest |
| Render a plan | Native CLI and Python 3 | Native CLI and Python 3; no Docker engine needed |

The launcher does not elevate privileges or choose a different Docker engine.
Privileged runs require explicit operator authorization. On macOS it checks for
OrbStack for portable workloads and refuses OVS execution on the host. Lima
keeps privileged state and high-I/O artifacts under `/var/lib/graphx`. No Docker,
OVS, or QMP socket is forwarded to macOS.

## Use

Build the native CLI with `cmake --preset dev && cmake --build --preset dev`.
From the repository root:

```sh
python3 scripts/instance.py plan examples/sample-pipeline/graphx.yaml
python3 scripts/instance.py up examples/sample-pipeline/graphx.yaml --build --port 28080
python3 scripts/instance.py status examples/sample-pipeline/graphx.yaml
python3 scripts/instance.py token examples/sample-pipeline/graphx.yaml
python3 scripts/instance.py down examples/sample-pipeline/graphx.yaml
```

The default instance is `demo`. Use `--set deployment.instance_id=lab-b` to
select another instance and a different `--port` for its published console.
Repeat the same `--set` values and `--state-root` on status, token, and down.
`GRAPHX_OVERRIDES` and `GRAPHX_BIN` are supported; explicit `--set` assignments
follow environment overrides. All settings pass through the authoritative loader.

`--build` builds the shared runtime/telemetry images and SDR image when selected.
Without it, the launcher uses existing images. Build trust uses `GRAPHX_CA_CERT`,
`GRAPHX_CERT_INSTALL_SCRIPT`, and `GRAPHX_BUILD_TRUST_FINGERPRINT`; environments
requiring additional trust should source `scripts/configure-build-trust.sh`
before invoking the launcher. `--no-history` disables bounded SQLite history;
`--no-capture` disables application capture. Capture is enabled only when the
configuration selects a provider. The raw SDR example has no packet observer or
application capture provider, so it does not claim packet capture coverage.

For the instance OVS sample in Lima, after starting and verifying the dedicated VM:

```sh
limactl shell --workdir /workspace/graphx-docker graphx -- sudo env \
  GRAPHX_BIN=/var/lib/graphx/runtime/build/dev/graphx \
  python3 scripts/instance.py up examples/sample-pipeline/ovs/graphx.yaml --build
# Use the same command with status or down for inspection and cleanup.
```

On native Linux, run that Python command with explicit root authorization and
`GRAPHX_BIN` pointing to the Linux CLI. The OVS sample's default instance is
`ovs-demo`. Its containers start behind execution gates; the existing owned OVS
lifecycle attaches their veth interfaces before application traffic is released.
The console is published on guest loopback. Lima's existing console forwarding
policy applies; arbitrary additional guest ports are not automatically forwarded.

## Configuration and adapter boundary

Each graph node must have exactly one `deployment.services` entry with an image
and supported command. Service count and IDs come from the graph, so adding a
second typed SDR source/controller pair does not require copying a Compose file.

| Command | Runtime adapter |
|---|---|
| `graphx-generator`, `graphx-transform`, `graphx-sink` | Native sample processes |
| `graphx-udp-publisher`, `graphx-udp-subscriber` | Native UDP processes |
| `sdr-source` | Typed SDR simulator |
| `sdr-controller` | Controller selected by its typed source's control edge |

The launcher requires explicit instance/project IDs, managed process/container
nodes, and TCP/UDP transports. The OVS profile supports container-veth attachments.
Unsupported commands, missing service selections, shared-memory/Unix transports,
and specialized namespace/TAP/capture/fault profiles are rejected before mutation.
Those laboratories retain their documented launchers. Inheriting the shared
Compose policy does not by itself make a compatibility laboratory instance-safe.
The root `graphx.yaml`, Compose files, and `scripts/demo.sh` remain compatibility
entry points used by the existing feature-overlay tests. The explicit-instance
sample lives under `examples/sample-pipeline/`.

The two-source SDR graph uses one private network namespace per instance, owned
by a small namespace-holder container and shared with its collector. The holder
keeps loopback connectivity stable across collector restart. Its loopback endpoints remain exactly as configured; another
instance can reuse them. Duplicate local endpoints and conflicts with collector
ports are rejected. This profile requires loopback sample/control endpoints.
It does not translate arbitrary external-device addresses into container DNS.
For managed TLS, source and controller credential references must be
`/run/NODE/FILENAME`. Startup provisions a separate seven-day demo CA and server/
client certificates for each source pair, with its configured server name. Node
containers receive only their selected telemetry secret and TLS files. These
certificates represent simulated peers, not physical SDR authentication.

## Ownership, failure, and storage

An instance lock serializes lifecycle commands. Runtime state is under
`.graphx/instances/<resource_key>` on macOS and
`/var/lib/graphx/runtime/instances/<resource_key>` on Linux by default. Use one
state root for the authority. Its root and instance directories must be owned
mode 0700. Per-instance credentials, history, captures, and execution gates stay
below that directory. Normalized configuration contains no secret material.

Startup provisions the existing version-2 runtime identity manifest, distinct
per-node telemetry secrets, and an instance-scoped operator policy. It registers
fresh executions through `graphx runtime activate`, creates stopped containers,
and records their immutable IDs before starting them. The collector receives a
read-only credential directory; each node receives only its own secret file.

A one-use gate persists across container starts. Docker automatic restart is
disabled, and a manual `docker restart` cannot run an already consumed execution.
Use `restart --node NODE` for isolated node reactivation in portable deployments,
for example:

```sh
python3 scripts/instance.py restart examples/sdr-node/two-source/graphx.yaml --node processor-east
python3 scripts/instance.py restart examples/sdr-node/two-source/graphx.yaml --node telemetry
```

Node restart stops/removes the recorded container, retires its execution, and
registers a fresh execution before creating its replacement. Peer registrations
remain unchanged. Collector restart preserves all node registrations. OVS container
replacement requires `down` followed by `up` through the owned infrastructure
lifecycle. The launcher does not continuously supervise crashed processes. The [two-instance acceptance gate](instance-acceptance.md) proves concurrent
SDR deployment, restart, checked recovery from interrupted startup, and isolated
shutdown. It does not add continuous process supervision.

Shutdown verifies the configuration digest, recorded container IDs, registration
scope, and management network IDs/endpoints before mutation. It uses the existing
OVS destroy operation for managed infrastructure, stops/removes the recorded
containers, retires exact executions, then removes the recorded management
network by ID. It preserves captures, history, credentials, and consumed gates.
It never uses broad project-name cleanup to remove containers.

A failed start retains its receipt and registrations. Failures before OVS creation
skip infrastructure destruction. Once OVS creation has been attempted, its ledger
is required for cleanup; if creation rolled back and removed that ledger, preserve
the runtime receipt and investigate rather than bypassing ownership checks.
Use `status` and `down`
with the original settings; do not overwrite active state or bypass identity
checks. An interruption between Docker creation and receipt publication leaves
unrecorded objects and deliberately requires investigation rather than adoption.
A loopback bind preflight rejects occupied published ports before provisioning.
Docker owns the subsequent port reservation; a racing collision fails startup
and does not stop its owner. Port conflicts and failures after partial acquisition leave
recoverable owned state, so a failed `up` still requires `down`.

## Verification

`graphx-compose-runtime` is part of the portable CTest suite. It checks normalized
rendering, per-node credential mounts, unsupported profiles, endpoint conflicts,
replacement refusal, protected files, and one-use execution gates.

After explicitly selecting a ready Docker engine and building current images:

```sh
python3 tests/test_compose_instance_live.py
```

This runs the sample pipeline and two-source SDR separately, checks traffic and
execution attribution, duplicate-start refusal, node/collector restart, shutdown, and fresh reactivation.
It does not constitute the full two-instance SDR acceptance scenario, OVS proof,
or QEMU guest execution. Run OVS acceptance separately on authorized Linux/Lima;
report native Linux, Lima, TCG, and KVM evidence separately.

Run `scripts/verify.sh instances` for the concurrent SDR resilience proof and
machine-readable evidence. See [instance acceptance](instance-acceptance.md).
