# Test reference

The CTest suite is grouped by purpose:

| Label | Coverage |
|---|---|
| `quick` | C++ units, configuration, normalized schema, projections, scripts, docs |
| `stress` | repeated transport lifecycle and concurrency pressure |
| `package` | installation, archive contents, consumer linkage, release contracts |

Telemetry uses Node's test runner. Docker scripts exercise the portable end-to-end
system, operations stack, secure OTLP, history, and hardening. Privileged Python
tests exercise current OVS ownership, container veth, network labs, QEMU TAP, and
network capture/fault behavior.
