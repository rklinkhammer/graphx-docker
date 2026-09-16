# Four-radio VITA development

[Implementation plan](implementation-plan.md) · [Accepted brief](../../docs/vita_system.md)

[P1.1 command analysis](command-analysis.md) records the vrtgen command mappings,
reproduced query-generation limitations and remaining command work.

The P1 standalone radio is an opt-in development target. It is not yet part of a
verified GraphX native release or OCI image. OVS integration and the four-radio
example remain later phases.

## Build and test the standalone radio

From the repository root, using a supported C++20 toolchain, CMake, Ninja, OpenSSL 3
and Python 3.13 (the generator environment tested on macOS):

```sh
python3.13 -m venv build/vita-tools
build/vita-tools/bin/python -m pip install -r scripts/vita/requirements.txt
cmake -S . -B build/vita -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_VITA_RADIO=ON \
  -DGRAPHX_VRTPKTGEN="$PWD/build/vita-tools/bin/vrtpktgen"
cmake --build build/vita --target graphx-cli graphx-vita-radio-app graphx-vita-device-test
ctest --test-dir build/vita -R graphx-vita --output-on-failure
```

The Python executable name may differ by installation. The configured generator
must belong to that virtual environment. CMake downloads hash-checked dependency
sources. Keep the virtual environment out of version control.

To see standalone packet/timing results directly:

```sh
python3.13 tests/test_vita_radio.py build/vita
```

The harness starts four independent radio processes with ephemeral loopback sockets,
creates a private catalog from the authoritative types, normalizes an authored graph
through `graphx`, stages temporary TLS credentials, issues VITA commands and decodes
actual UDP bytes independently. It stops only its own processes and removes its
private temporary files. It does not run Docker, OVS, physical radios or privileged
operations. It intentionally acts as both the test controller and packet receiver;
P2 supplies the production processor/controller.

The executable accepts the existing application arguments:
`--node ID --config NORMALIZED_NODE_JSON --release-file FILE --release-token TOKEN`.
Use the harness rather than hand-writing normalized nodes or bypassing the release
barrier. `GRAPHX_CREDENTIALS` points to its private staged credential root. A
production managed invocation also needs the existing staged telemetry credential.

See [radio-design.md](radio-design.md) for the implemented profile and current limits,
and [p1-verification.md](p1-verification.md) for evidence and remaining P1 work.
