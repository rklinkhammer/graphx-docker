# Test procedure

Use the smallest profile that covers the change:

```sh
scripts/verify.sh quick
scripts/verify.sh quality
scripts/verify.sh sanitizers
scripts/verify.sh fuzz
scripts/verify.sh portable
scripts/verify.sh full
scripts/verify.sh native-linux
scripts/verify.sh release
```

- `quick` builds and runs unit and portable contract tests.
- `quality` runs formatting, clang-tidy, and cppcheck on the host toolchain.
- `sanitizers` runs the LLVM 21 address/undefined-behavior suite supported by the host.
- `fuzz` runs bounded LLVM 21 libFuzzer smoke tests.
- `portable` runs complete non-Docker acceptance with C++20.
- `full` adds sanitizers, fuzzing, Docker acceptance, and, when invoked on macOS,
  the Linux Clang 21/libstdc++ 15 quality container.
- `native-linux` runs OVS, veth, namespace, TAP, capture, and fault lifecycles.
- `release` builds and independently verifies a local release candidate from a clean tree.

Run the Linux quality environment directly on any Docker host with:

```sh
scripts/test-linux-container.sh quality
```

Its `quality` mode contains formatting and static analysis only. Fuzzing remains
the separate `fuzz` mode and is not duplicated when `verify.sh full` runs both.

Run privileged tests only on native Linux or inside the GraphX Lima guest. Report
host architecture, guest architecture, and QEMU accelerator with results. A passing
test run must leave no GraphX Compose projects, OVS resources, namespaces, TAP/veth
devices, capture processes, qdiscs, or temporary state.

Portable and full acceptance require Node.js 24.x and fail before installing
dependencies when another Node.js major version is selected. Privileged workflows
are registered as CTests with the `privileged` label; `native-linux` configures that
test set and runs it after portable acceptance.
