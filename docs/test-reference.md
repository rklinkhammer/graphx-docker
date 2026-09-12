# Test reference

The CTest suite is grouped by purpose:

| Label | Coverage |
|---|---|
| `quick` | C++ units, configuration, normalized schema, scripts, and documentation |
| `stress` | repeated transport lifecycle and concurrency pressure |
| `package` | installation, archive contents, consumer linkage, release contracts |
| `privileged` | native Linux OVS, veth/TAP, namespaces, capture, faults, SDR, and routes |

Telemetry uses Node's test runner. Docker scripts exercise the portable end-to-end
system, operations stack, secure OTLP, history, and hardening. Privileged Python
tests exercise current OVS ownership, container veth, network labs, QEMU TAP,
network capture/fault behavior, SDR delivery, and route policy. CTest fixtures own
setup and cleanup for multi-step laboratories.
