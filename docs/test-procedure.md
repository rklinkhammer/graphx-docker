# Test procedure

Use the smallest profile that covers the change:

```sh
scripts/verify.sh quick
scripts/verify.sh quality
scripts/verify.sh portable
scripts/verify.sh full
scripts/verify.sh native-linux
```

- `quick` builds and runs unit and portable contract tests.
- `quality` runs formatting, clang-tidy, and cppcheck on the host toolchain.
- `portable` runs complete non-Docker acceptance with C++20 and C++23.
- `full` adds sanitizers, fuzzing, Docker acceptance, and, when invoked on macOS,
  the Linux Clang 21/libstdc++ 15 quality container.
- `native-linux` runs OVS, veth, namespace, TAP, capture, and fault lifecycles.

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
