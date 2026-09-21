# CMake example targets

From the checkout you intend to run:

```sh
cmake --preset dev
cmake --build --preset dev --target examples-list
cmake --build --preset dev --target sample-pipeline-plan
cmake --build --preset dev --target sample-pipeline-prepare
cmake --build --preset dev --target sample-pipeline-up
cmake --build --preset dev --target sample-pipeline-status
cmake --build --preset dev --target sample-pipeline-down
```

CMake discovers every `examples/**/graphx.yml`. Slashes in example names become
hyphens: `sample-pipeline/ovs` becomes `sample-pipeline-ovs`. No separate example
manifest is maintained. Targets also appear in the IDE's CMake target picker.
Ordinary configure/build/CTest commands do not launch these targets.

| Target suffix | Behavior |
|---|---|
| `plan` | Build the CLI and validate the selected example; no infrastructure mutation |
| `prepare` | Build the CLI, stop/replace this instance if prepared, build fresh no-cache images and compile |
| `up` | Build the CLI, reuse preparation, restart this instance with fresh owned processes |
| `status`, `open`, `logs` | Inspect state, open the authenticated console, or print node logs |
| `down` | Perform identity-checked cleanup; stop Lima when idle |
| `scenario-plan`, `scenario-run`, `scenario-status`, `scenario-clear` | Invoke the explicitly selected authored action |

Run targets sequentially. Do not request `prepare up down` in a single build;
they are independent operations, not a dependency chain. Shutdown, inspection,
and scenario targets do not rebuild the CLI or images. They require an already
built CLI. This keeps cleanup available when edited C++ sources cannot compile.

`prepare` invokes the common CLI with `--restart --fresh-images`. Each invocation
uses a new artifact directory, builds without cache, and verifies the release.
`up` uses `--restart` and reuses that preparation. For container verification,
prepare once per run and use `up`/`down` for each case. Calling `up` alone retains
the CLI's normal artifact-cache behavior. Native-only examples build their native
release; QEMU examples also prepare their required guest artifacts.

## Select the environment and options

Settings persist in the CMake build directory. The default target is selected by
the existing CLI: macOS uses Lima for privileged graphs, OrbStack for portable
containers, and native macOS for native graphs; Linux selects native Linux.
The selected Docker engine must be ready for container builds.

For an explicitly authorized Lima example:

```sh
cmake --preset dev -DGRAPHX_EXAMPLE_TARGET=lima -DGRAPHX_EXAMPLE_ALLOW_PRIVILEGED=ON
cmake --build --preset dev --target four-radio-vita-prepare
cmake --build --preset dev --target four-radio-vita-up
cmake --preset dev -DGRAPHX_EXAMPLE_SCENARIO=iq-loss-jitter
cmake --build --preset dev --target four-radio-vita-scenario-plan
cmake --build --preset dev --target four-radio-vita-scenario-run
cmake --build --preset dev --target four-radio-vita-down
```

Lima startup, source staging and idle shutdown use the existing identity checks.
Use the checkout bound to that VM. CMake does not retarget a VM created for a
different checkout. Runtime artifacts remain under `/var/lib/graphx` in Linux.
No scenario is applied by ordinary `up`; unsupported graphs/actions fail through
the authoritative CLI. Physical SDR remains gated by its ownership contract.

| CMake cache setting | Corresponding selection |
|---|---|
| `GRAPHX_EXAMPLE_TARGET` | `lima`, `orbstack`, `native-linux`, `native-macos`, or empty for host default |
| `GRAPHX_EXAMPLE_ALLOW_PRIVILEGED` | `ON` passes `--allow-privileged`; default `OFF` |
| `GRAPHX_EXAMPLE_WORKSPACE`, `GRAPHX_EXAMPLE_INSTANCE` | Existing CLI workspace and instance selection; keep consistent through cleanup |
| `GRAPHX_EXAMPLE_IMAGES`, `GRAPHX_EXAMPLE_RELEASE`, `GRAPHX_EXAMPLE_CATALOG` | Existing verified artifacts; paths are guest-local for Lima |
| `GRAPHX_EXAMPLE_EXTERNAL`, `GRAPHX_EXAMPLE_LABORATORY` | External credential directory and explicit laboratory selection |
| `GRAPHX_EXAMPLE_CONTROL` | Semicolon-separated grants, e.g. `generator:pause,resume;collector:reset` |
| `GRAPHX_EXAMPLE_NODE` | Node to inspect with `logs` |
| `GRAPHX_EXAMPLE_SCENARIO` | Authored scenario action ID; required for scenario targets |

Quote settings containing spaces or semicolons. Clear a setting with, for example,
`cmake --preset dev -DGRAPHX_EXAMPLE_IMAGES=`. An explicit image, release, or catalog
selection disables fresh artifact building during CMake preparation and follows
the CLI's artifact-override behavior. Privilege opt-in persists until set `OFF`.
Use separate CMake build directories when maintaining different settings in parallel.

Advanced options remain available through `build/dev/graphx example`. The CMake
targets delegate to that same workflow; there is no separate container or network
implementation. For direct CLI use, `--fresh-images` is accepted only with
`prepare`/`up`, without artifact overrides, and requires `--restart` when an instance
already has a prepared generation. Failed builds retain evidence and ownership
records for normal `down` recovery; no global prune is performed.
