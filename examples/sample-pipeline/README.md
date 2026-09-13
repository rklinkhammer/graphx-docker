# Sample pipeline

A generator sends samples through a transform to a sink. Telemetry provides the
web console, with optional capture, history, control, and observability overlays.

**Linux:** Docker Engine with Compose. **macOS:** OrbStack with Compose. This
portable example does not require Lima or privileged OVS networking.

From the repository root:

```sh
examples/sample-pipeline/scripts/demo.sh start
examples/sample-pipeline/scripts/demo.sh verify
examples/sample-pipeline/scripts/demo.sh stop
```

Use `status`, `logs`, and `token` with the same launcher for inspection. See the
[walkthrough](../../docs/complete-system-demo.md) for credentials and options.

The topology and all Compose overlays live in this directory. For direct Compose
use, select the file explicitly from the repository root:

```sh
docker compose -f examples/sample-pipeline/compose.yaml config --quiet
```

Or run `docker compose` from this directory. Build contexts and shared configuration
mounts resolve relative to the Compose file. Set `GRAPHX_CONFIG_FILE` to an absolute
path when supplying another topology. Shared images still build from the repository
Dockerfiles; the demo does not carry copies of them.

| File | Purpose |
|---|---|
| `graphx.yaml` | Generator, transform, sink, transports, and telemetry settings |
| `compose.yaml` | Base pipeline and console |
| `compose.history.yaml` | Persistent SQLite history (enabled by the launcher) |
| `compose.control.yaml` | Scoped control policy and per-node credentials |
| `compose.control-rotation.yaml` | Previous credentials during rotation |
| `compose.observability.yaml` | Prometheus and Grafana |
| `compose.otlp-secure.yaml` | Authenticated HTTPS OTLP export |
| `compose.otlp-mtls.yaml` | OTLP client certificates |

The launcher keeps its credentials in the repository's `.graphx/` directory and
uses the Compose project `graphx`. Configuration stays version 2; no generation
step is needed to edit or run this example.
