# Complete system workflow

Use authored v3 graphs, deterministic compilation, verified releases and
[owned execution](execution.md). Follow the [user guide](user-guide.md) and
[example matrix](../examples/README.md) for native and portable container cases.

Compiled OVS and namespace execution requires explicit local Linux privileged
opt-in. Its S07–S12 Lima acceptance and compiled profile wrappers are verified; see
[P7 verification](../design/graph-generation/p7-verification.md). Physical radio
uplinks, owned QEMU boot and scenario actions retain their explicit gates in the
[implementation plan](../design/graph-generation/implementation-plan.md).

Portable verification covers C++ behavior, normalized consumers, telemetry HTTP,
security/history/capture, web tests/build and the native execution lifecycle.
Unprivileged Linux compilation is separate from privileged networking acceptance
and does not prove a complete Linux laboratory deployment.
