# Portable graph execution

`graphx run up` consumes an existing compilation and a verified release. It checks
artifact hashes, target, executable/image identity, resource paths and listener
ports before launching applications. Native and container applications must use
separate graphs. OVS, namespace, QEMU and scenario-action execution remain gated.

Startup is finite: stage credentials, start the platform, wait for listeners,
credentials and SQLite history, start applications held at their local readiness
barrier, verify every process, and publish the ownership token to release traffic.
The command then returns. It does not supervise, restart or repair applications.
`status` inspects recorded identities; `down` stops only owned resources.

## Native installation and execution

Build and verify the native and platform companion archives as described in
[release-process.md](release-process.md). Install archives from the same commit,
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

## Container execution

Confirm the selected engine with `docker info` and `docker compose version`.
On macOS select OrbStack. Compile with the verified image release's catalog,
using the release catalog workflow in [release-process.md](release-process.md).
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

## Recovery and examples

If readiness fails, inspect the returned error and the graph's ownership file and
logs. Run `down` with the same compilation to finish interrupted cleanup. If an
identity differs, recover the original resource or investigate the mismatch;
do not rewrite identities to make cleanup pass. A missing retained history volume
also requires investigation. Existing unrelated workloads are left untouched.

Portable example wrappers call this same adapter. Set `GX_OUTPUT`, `GX_STATE`,
`GRAPHX_BIN` (or `GX_RELEASE`), and the applicable credential/image roots, then
invoke the example's `run.sh` or `scripts/demo.sh` with `up`, `status`, or `down`.
Compilation is a separate explicit step. Source Compose includes are inspection
conveniences; use `graphx run` for owned staging, readiness and release.
