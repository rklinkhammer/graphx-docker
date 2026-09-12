# Test procedure

Use the smallest profile that covers the change:

```sh
scripts/verify.sh quick
scripts/verify.sh portable
scripts/verify.sh full
scripts/verify.sh privileged-linux
```

- `quick` builds and runs unit and portable contract tests.
- `portable` adds analyzers, telemetry tests, schema checks, and packaging checks.
- `full` adds Docker acceptance and container hardening.
- `privileged-linux` runs OVS, veth, namespace, TAP, capture, and fault lifecycles.

Run privileged tests only on native Linux or inside the GraphX Lima guest. Report
host architecture, guest architecture, and QEMU accelerator with results. A passing
test run must leave no GraphX Compose projects, OVS resources, namespaces, TAP/veth
devices, capture processes, qdiscs, or temporary state.
