# GraphX user guide

The implemented graph workflow validates an authored v3 graph and produces
resolved JSON. Start from one of the [24 examples](../examples/README.md), select
a target, and inspect the normalized model. Direct applications also support
[resolved node bindings and a local release barrier](configuration.md#application-bindings);
artifact compilation is available and graph execution adapters remain gated.

Build and validate from the repository root:

```sh
cmake --preset dev
cmake --build --preset dev
build/dev/graphx validate examples/sample-pipeline/graphx.yml --target orbstack
build/dev/graphx inspect examples/sample-pipeline/graphx.yml --target orbstack
```

This minimal container pipeline is a complete authored graph. Save it as
`examples/my-pipeline/graphx.yml`; the catalog path is relative to that file.

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

Names identify instances, not compiled-in application roles. Types declare their
ports and supported encodings. The loader resolves defaults, checks connection
counts and emits shared endpoint settings. See the
[configuration contract](configuration.md) for limits, credentials, network
attachments, catalog trust roots and target selection.

```sh
build/dev/graphx config normalize examples/sample-pipeline/graphx.yml --target orbstack > resolved.json
```

The normalized JSON contains configuration and credential IDs. It contains no
credential material and creating it starts no processes. SQLite history is enabled
by default with bounded queues, retention and database size. Application capture,
OVS capture and telemetry defaults are visible in `platform`.

Execution commands and example launchers return `E_PHASE_UNAVAILABLE`. Generic
application bindings, artifact generation, platform staging, process orchestration
and OVS/QEMU realization have separate gates in the
[phased plan](../design/graph-generation/implementation-plan.md). Checked-in Compose
recipes and shell bodies are not a supported v3 launch path. Do not interpret a
successful target validation as Linux, OrbStack, Lima or guest execution evidence.

Use `scripts/verify.sh quick`, `scripts/verify.sh quality`, and
`scripts/verify.sh portable` for the implemented contract and reusable modules.
The [test procedure](test-procedure.md) describes prerequisites and separates
privileged evidence, which always requires explicit authorization.

## Compile inspectable artifacts

Choose a fresh output outside the repository and the protected credential root:

```sh
COMPILE_PARENT=$(python3 -c 'import pathlib,tempfile; print(pathlib.Path(tempfile.mkdtemp(prefix="graphx-compile-")).resolve())')
build/dev/graphx compile examples/sample-pipeline/graphx.yml \
  --target native-linux --source-root "$(pwd -P)" \
  --credential-root "$COMPILE_PARENT/credentials" --output "$COMPILE_PARENT/sample"
```

Inspect `compile-manifest.json`, `resolved.json`, `nodes/`, and the emitted plans.
Compilation starts nothing. Image/release pins remain explicitly unverified until
packaging, and graph execution stays gated. Recompilation requires a fresh output
directory; existing output and unrelated files are preserved.
