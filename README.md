# GraphX

GraphX 1.1.0 describes typed processing graphs, runs them in controlled laboratories,
and shows application traffic and declared network paths in a browser console.
Supported workflows include native processes, portable containers, Linux OVS
networks, verified QEMU TCG guests and explicitly selected simulator scenarios.
Startup and cleanup verify resource ownership; runtime operation is not continuously
supervised or automatically repaired.

## Start here

- [User guide](docs/user-guide.md): installation, concepts and complete workflows.
- [Run your first example](examples/quick-start.md): a continuous sample pipeline with automatic console login.
- [Choose a demo](docs/user-guide.md#example-selection) and [complete target matrix](examples/README.md).
- [Documentation index](docs/README.md): operational and technical references.
- [Architecture review](docs/GraphX_Architecture.md) and [review checklist](docs/GraphX_Architecture.md#architecture-review-checklist).

## Build and validate

From the repository root with the [development prerequisites](docs/user-guide.md#installation-and-prerequisites):

```sh
cmake --preset dev
cmake --build --preset dev
build/dev/graphx validate examples/sample-pipeline/graphx.yml --target orbstack
```

Validation starts no processes. Authored graphs use `version: 3`; normalized JSON
uses contract version 2. The C++ loader resolves the authoritative catalog and
shared endpoint settings. [Configuration](docs/user-guide.md#configuration-reference) explains this contract.

## Run the sample

After completing the [quick start prerequisites](examples/quick-start.md), with a
ready Docker engine (OrbStack on macOS), run:

```sh
build/dev/graphx example up sample-pipeline \
  --control generator:pause,resume --control collector:reset
build/dev/graphx example open sample-pipeline
build/dev/graphx example down sample-pipeline
```

`up` prepares verified local artifacts when necessary and opens an authenticated
console. The sample runs until stopped. `open` reopens the same running graph;
refresh preserves its browser session. Use `--json` or `--no-open` for scripts.
Read the [CLI reference](docs/user-guide.md#cli-reference) before selecting custom artifact inputs.

Managed data-plane networking uses system Open vSwitch on Linux. Containers attach
with owned veth pairs and QEMU guests use TAP devices. Docker Compose manages
processes and management connectivity. On macOS, OVS laboratories use the dedicated
Lima guest and require explicit privileged authorization; they do not run in OrbStack.
Physical SDR startup remains gated pending its uplink ownership contract.

## Verification and support

Available verification profiles are `quick`, `quality`, `sanitizers`, `fuzz`,
`portable`, `full`, `native-linux`, and `release`. Their requirements are documented in
[the test procedure](docs/test-procedure.md). [Documentation verification](docs/documentation-verification.md)
separates current unprivileged checks from prior runtime evidence and open release gates.

Use [troubleshooting](docs/user-guide.md#troubleshooting) for startup, credentials, missing
traffic and identity refusal. Read [CONTRIBUTING.md](CONTRIBUTING.md),
[SUPPORT.md](SUPPORT.md) and [SECURITY.md](SECURITY.md) before submitting changes or sharing diagnostics.
