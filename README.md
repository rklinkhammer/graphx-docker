# GraphX

GraphX 1.1.0 describes typed processing graphs, runs them in controlled laboratories,
and shows application traffic and declared network paths in a browser console.
This README is the single setup and build reference for native development,
container images, example targets, and the macOS Lima environment.

## Build environment

Run the commands in this guide from a full GraphX repository checkout. The example
workflow requires the checkout even when using an installed `graphx` binary;
`--source /absolute/path/to/checkout` selects it explicitly. Lima source transfer
currently requires a full `.git` directory rather than a linked Git worktree.

| Environment | Requirements and intended use |
|---|---|
| Development host | Git, Python 3, CMake 3.25 or newer, Ninja, a C++20 compiler, OpenSSL 3 development files and Node.js 26.x with npm. Linux Node runtimes also require `libatomic1`. Dependency preparation needs network access unless the required inputs are already cached. |
| Native macOS | Native-process examples such as shared memory, UDP unicast/multicast and application capture. Install the compiler through Apple's developer tools and the other prerequisites through your preferred package manager. |
| OrbStack on macOS | Running OrbStack Docker engine, Docker CLI, Compose v2 and Buildx for portable container examples. Select `docker context use orbstack` before these examples. It is not the managed OVS backend. |
| Native Linux | A local Docker engine with Compose/Buildx for containers; native examples need a matching native/platform release. Privileged OVS examples additionally need system OVS, iproute2, namespaces, nftables and the capture/network tools used by their selected scenario. |
| GraphX Lima guest | Apple Silicon host with Lima installed. The checked-in VM provisions the Linux toolchain and privileged laboratory dependencies. See [Lima setup](infrastructure/lima/README.md); `example prepare` and `example up` create/start the VM automatically. |
| Managed QEMU | Verified guest artifacts and combined native/platform/guest installation, QEMU TCG and the dedicated Linux `graphx-qemu` account at UID/GID 65532. See [guest prerequisites](guests/README.md). A TAP lifecycle check alone does not prove guest execution. |

These are tool requirements, not a claim that every operating-system distribution
or CPU combination has been qualified. See [verification scope](docs/documentation-verification.md).

For Apple Silicon with Homebrew Node 26, select that runtime before building:

```sh
export PATH=/opt/homebrew/bin:$PATH
```

On other hosts, put your installed Node 26 on PATH. Then build the development CLI:

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
Verified release installation is covered in [execution](docs/user-guide.md#execution-administration-native-installation-and-execution).

## Build all examples

Start the selected Docker engine first (OrbStack on macOS). Run from the repository root:

```sh
graphx artifacts build
```

This explicit command builds all shared container roles
(runtime, telemetry, SDR and VITA), the echo/radio x86-64 QEMU kernel and initramfs
artifacts, the host Node/web platform bundle, and every authored example graph.
Graph generation targets `native-linux` so Linux-only examples can be inspected
from macOS. It starts only temporary build/smoke containers; it does not boot QEMU,
start Lima, launch a graph or configure privileged networking.

Images are built without cache once per source revision and shared across all
examples. QEMU uses the reviewed Buildroot cross-build recipe. A first build can
take substantial time and disk space. Dependency downloads require network access,
including access to the pinned VRT Git repository for VITA.

Artifacts live outside the checkout because the QEMU builder requires that boundary.
The default root is `~/.cache/graphx/build/<checkout-hash>`; use `--output` to select
another external path and `--platform linux/amd64` to select image architecture.
The build prints its completed generation directory: `images/` holds verified OCI
images and a catalog, `guests/` holds QEMU artifacts and the combined catalog,
`platform/` holds the host platform archive, and `compiled/` holds all graph outputs.
`current.json` selects the last complete generation. Failed builds retain their
own evidence and do not replace it. An unchanged rebuild verifies and reuses that
complete generation. To force a fresh verification release:

```sh
graphx artifacts build --fresh
```

The compatibility targets `examples-build` and `examples-rebuild` delegate to the
same command with a build-directory-scoped artifact root. QEMU guests remain x86-64
TCG artifacts. Building them does not establish guest-boot acceptance. Ordinary
development builds never build these artifacts and do not require Docker:

```sh
cmake --preset dev
cmake --build --preset dev -j 4
```

Artifact, verification and laboratory operations always require an explicit command.

## Organization certificates for local Docker builds

For a private CA or a reviewed Linux certificate installer, set absolute paths on
the machine running the build (omit either option if unnecessary):

```sh
export GRAPHX_CA_CERT="/absolute/path/company-root-ca.crt"
export GRAPHX_CERT_INSTALL_SCRIPT="/absolute/path/install-certs.sh"
graphx artifacts build
```

The shared image builder (including VITA/SoapySDR), QEMU builder and Linux verifier
pass these files as BuildKit secrets. The installer runs inside the build container,
not on the host. Its resulting system CA bundle is inherited by GraphX runtime
images. Trust file contents participate in build cache invalidation and aggregate
artifact reuse. Certificate paths must not contain commas or newlines.

These settings do not modify Docker daemon trust for registry pulls, upstream
Prometheus/Grafana images, or host-side downloads. For Lima builds, configure paths
inside the guest; host environment variables and files are not automatically forwarded.
Automatic per-example image caches also distinguish certificate contents; explicitly
supplied `--images` releases remain the caller's selected artifacts.
Public CI builds use the default system trust unless explicitly configured otherwise.

## Linux example workspace setup

On a native Linux host, examples use `/var/lib/graphx/examples` by default. Before
the first example command, create this workspace for the normal user who will run
GraphX. Otherwise startup can fail with `Permission denied: '/var/lib/graphx'`.

For a fresh installation where `/var/lib/graphx/examples` does not exist, run:

```sh
sudo install -d -m 0700 \
  -o "$(id -u)" -g "$(id -g)" \
  /var/lib/graphx/examples
```

If the directory already exists, inspect its ownership before making changes:

```sh
ls -ld /var/lib/graphx /var/lib/graphx/examples
```

Do not recursively change ownership of an existing workspace or its runtime state.
Run subsequent `graphx example` commands as the same normal user. For privileged
examples, `--allow-privileged` authorizes the operation and GraphX invokes `sudo`
for the execution step; it does not grant permission to create the workspace.
Running the entire workflow with `sudo` creates root-owned workspace files.

This step creates the workspace only; the Linux Docker, OVS and scenario-specific
prerequisites listed above must also be installed.

## Prepare and run an example

For the portable sample, with a ready Docker engine (OrbStack on macOS):

```sh
graphx example prepare sample-pipeline --restart --fresh-images \
  --control generator:pause,resume --control collector:reset
graphx example up sample-pipeline --restart \
  --control generator:pause,resume --control collector:reset
graphx example status sample-pipeline
graphx example open sample-pipeline
graphx example down sample-pipeline
```

For the four-radio VITA example, after authorizing privileged Linux/Lima work:

```sh
graphx example prepare four-radio-vita --target lima --allow-privileged \
  --restart --fresh-images
graphx example up four-radio-vita --target lima --allow-privileged --restart
graphx example status four-radio-vita --target lima --allow-privileged
graphx example down four-radio-vita --target lima --allow-privileged
```

Select `native-linux` instead of `lima` for authorized native Linux execution.
Use [the example matrix](examples/README.md) to choose another graph and target.
The [four-radio guide](examples/four-radio-vita/README.md) explains its FFT,
statistics, loss/jitter scenario and recovery behavior.

The workflow discovers every `examples/**/graphx.yml`; no separate example manifest
is maintained. CMake retains only `examples-list`, `examples-build`, and
`examples-rebuild` as convenience targets. Runtime operations use the CLI directly.

| Command | Behavior |
|---|---|
| `graphx example plan NAME` | Validate the selected example; no infrastructure mutation |
| `graphx example prepare NAME` | Prepare artifacts and compile without starting |
| `graphx example up NAME` | Reuse preparation and start owned processes |
| `graphx example status/open/logs NAME` | Inspect state, open the console, or print node logs |
| `graphx example down NAME` | Perform identity-checked cleanup; stop Lima when idle |
| `graphx example scenario NAME --operation OP --action ID` | Invoke an explicitly selected authored action |

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

### Example settings

Pass settings to each lifecycle command so the selected identity remains explicit.
The default target is selected by the CLI: macOS uses Lima for privileged graphs, OrbStack for portable
containers, and native macOS for native graphs; Linux selects native Linux.
The selected Docker engine must be ready for container builds.

For an explicitly authorized Lima example:

```sh
graphx example prepare four-radio-vita --target lima --allow-privileged
graphx example up four-radio-vita --target lima --allow-privileged
graphx example scenario four-radio-vita --target lima --allow-privileged \
  --operation plan --action iq-loss-jitter
graphx example scenario four-radio-vita --target lima --allow-privileged \
  --operation run --action iq-loss-jitter
graphx example down four-radio-vita --target lima --allow-privileged
```

Lima startup, source staging and idle shutdown use the existing identity checks.
Use the checkout bound to that VM. CMake does not retarget a VM created for a
different checkout. Runtime artifacts remain under `/var/lib/graphx` in Linux.
No scenario is applied by ordinary `up`; unsupported graphs/actions fail through
the authoritative CLI. Physical SDR remains gated by its ownership contract.

| CLI option | Selection |
|---|---|
| `--target` | `lima`, `orbstack`, `native-linux`, `native-macos`, or omitted for host default |
| `--allow-privileged` | Explicitly authorizes Linux/Lima privileged operations |
| `--workspace`, `--instance` | Existing workspace and instance selection; keep consistent through cleanup |
| `--images`, `--release`, `--catalog` | Existing verified artifacts; paths are guest-local for Lima |
| `--external`, `--laboratory` | External credential directory and explicit laboratory selection |
| `--control NODE:ACTIONS` | Repeatable control grant, for example `generator:pause,resume` |
| `--node` | Node to inspect with `logs` |
| `--action` | Authored scenario action ID; required for scenario operations |

An explicit image, release, or catalog selection disables fresh artifact building
during preparation and follows the CLI's artifact-override behavior. There is no
separate CMake container or network implementation. `--fresh-images` is accepted only with
`prepare`/`up`, without artifact overrides, and requires `--restart` when an instance
already has a prepared generation. Failed builds retain evidence and ownership
records for normal `down` recovery; no global prune is performed.

## macOS Lima environment

The checked-in definition uses Apple Silicon Virtualization.framework, ARM64
Ubuntu, four CPUs, 8 GiB RAM and an 80 GiB disk. It mounts this repository at
`/workspace/graphx-docker` and installs rootful Docker, Compose, Buildx, system OVS,
Node.js 26 and Linux packet tools. Docker and other privileged sockets are never
forwarded to macOS. The host Docker context remains OrbStack.

`graphx example prepare` and `graphx example up` create or start the dedicated
VM automatically, using the checkout selected by the command. Install Lima first;
the first preparation provisions Linux and builds the required artifacts.
`example prepare` and `example down` stop the VM after success when no containers,
application processes or laboratory networks remain. Its disk and prepared images
are retained for the next launch. A failed operation or uncertain idle check leaves
the VM running for inspection and recovery.

`graphx env up` and `graphx env down` remain manual overrides. Use `env up` to inspect
retained guest state after an automatic stop. Status, logs and browser commands do
not start a stopped VM. The example CLI serializes Lima operations across checkouts;
complete a following-log command before another lifecycle command. Direct guest
commands and low-level laboratory scripts require manual environment management;
do not run them concurrently with automatic demo shutdown.

VM identity includes the absolute host checkout path. A VM belonging to a different
checkout is refused; it is never replaced automatically. The guest mount path
`/workspace/graphx-docker` does not require that directory name on the host.

The host console port is normally `18080`, forwarded to guest loopback `8080`.
Build artifacts, ownership ledgers, history and captures stay on the guest disk
under `/var/lib/graphx`. Status and log commands do not start a stopped VM.
Use `graphx env up` for inspection and `graphx env down` to stop it while retaining
its disk. An interrupted operation is recovered with the same instance and
workspace through `example down`; never rewrite ownership records or prune
unrelated resources.

The VM identity covers the checkout path, architecture, VM type, `graphx.yaml`
and `provision.sh`. A VM created for a different checkout or provisioning version
is refused. Node 26.9.0 provisioning therefore requires recreating an old Node 24
VM: clean up using its matching original checkout, stop it, then delete the VM
before preparing from this checkout. VM deletion removes its guest disk contents.
See [Lima diagnostics](infrastructure/lima/README.md) for inspection commands.

## Verify the build

```sh
scripts/verify.sh quick
scripts/verify.sh quality
scripts/verify.sh portable
```

Available verification profiles are `quick`, `quality`, `sanitizers`, `fuzz`,
`portable`, `full`, `native-linux`, and `release`. Their requirements and scope
selection are in [the test procedure](docs/test-procedure.md). `full` requires a
ready Docker engine; privileged Linux/Lima tests require explicit authorization.
Normal CMake builds do not authorize or launch these tests.

Quality checks require LLVM 21, clang-format, clang-tidy and cppcheck. On macOS,
select a compatible SDK if the active Xcode SDK cannot compile with LLVM 21:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk \
  scripts/verify.sh quality
```

Use that override only when this SDK is installed. Node 26.x must be on PATH for
portable checks. Container and platform releases pin Node 26.9.0 with checksums;
container builds require network access to the pinned base images and dependencies.
Release packaging and artifact overrides use the common
[release administration](docs/user-guide.md#release-administration) workflow.

## Further reading

- [User guide](docs/user-guide.md): application concepts, CLI, credentials and operation.
- [Run your first example](examples/quick-start.md): sample behavior and console use.
- [Documentation index](docs/README.md), [architecture](docs/GraphX_Architecture.md),
  and [verification scope](docs/documentation-verification.md).
- [Troubleshooting](docs/user-guide.md#troubleshooting), [contributing](CONTRIBUTING.md),
  [support](SUPPORT.md), and [security](SECURITY.md).
