# Complete system workflow

The current supported workflow is authored v3 validation and normalized v2 inspection.
Follow the [user guide](user-guide.md) and [example matrix](../examples/README.md).

A complete graph launch is unavailable in P1. Sample launchers return
`E_PHASE_UNAVAILABLE` before credentials, containers or infrastructure are created.
Generic application bindings, platform staging, orchestration and scenario actions
have explicit gates in the [implementation plan](../design/graph-generation/implementation-plan.md).

The portable verification profile exercises reusable C++ transport and ownership
modules, strict graph normalization, telemetry HTTP/security/history/capture tests,
and the web console. This is not evidence of an orchestrated v3 deployment.
