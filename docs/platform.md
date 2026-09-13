# Default platform and credential staging

Every compiled graph has one platform configuration. `graphx-platform --config
<output>/platform.json` consumes that file and the sibling `resolved.json` and
`credentials.json`. The C++ resolver remains authoritative; the platform verifies
that its configuration agrees with normalized contract 2. It never normalizes
YAML at startup. `graphx run` stages credentials and starts this platform before releasing native
or portable container applications; see [execution](execution.md). Infrastructure
execution remains gated. `/api/ready` requires history to be ready (or disabled),
as well as HTTP/UDP listeners and valid credentials.

The native platform companion archive installs `bin/graphx-platform`, pinned Node
24.20.0, telemetry modules, production dependencies and built web assets alongside
the native C++ release. Put that release's `bin` directory on `PATH`. The shared
Dockerfile has `runtime`, `telemetry` and `sdr` targets; telemetry and SDR reuse the
C++ runtime. All run as UID/GID 65532. No graph is baked into an image.

## Explicit provisioning

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
consumer, not the complete staging root. Privileged Linux guest staging and graph
lifecycle orchestration belong to the later authorized execution adapters.

## Platform lifecycle and access

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

Deletion checks all selected history paths before removing only SQLite's database,
WAL and SHM files. It keeps the stable lock file and does not delete captures,
credentials or application state. It is never part of routine stop/down.

The console requires the `observer/token` credential for graph reads, including
on loopback. Readiness endpoints expose only bounded health information. Compiled
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
Grafana reads its external `grafana-admin/password` file. Native execution of
optional external service processes remains part of the execution adapter work.

## Rotation and bounds

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
TLS sessions retain their negotiated identity until reconnect. Scenario dispatch
of rotation actions remains P9 work.

History reserves storage for the database, WAL frames and SHM together. It refuses
new writes when checkpointing cannot reclaim storage and reports drops without
stopping live telemetry. Queue count/bytes, pending queries, query deadlines and
shutdown are bounded. A query deadline degrades history and terminates its worker
instead of allowing expired queries to refill an unbounded worker queue. Existing
files that exceed a reduced storage allocation are rejected before service use.
