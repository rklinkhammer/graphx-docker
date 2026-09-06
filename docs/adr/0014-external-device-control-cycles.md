# ADR 0014: external device attachment and control relationships

- Status: Accepted for Phase 13
- Date: 2026-09-06

## Context

An Ethernet SDR sends ordinary UDP sample blocks to a processor. The processor
uses a reliable authenticated TCP connection in the opposite direction for
`start`, `stop`, `tune`, and `status`. Together these two physical relationships
form a directed cycle. GraphX configuration version 1 previously rejected every
cycle because its managed blocking runtime cannot schedule cyclic execution.
That rationale does not apply to edges whose data plane is owned by external
applications and which the GraphX transport factory is forbidden to construct.

The physical SDR is independently owned. A demonstration may provide a bounded
simulator, but GraphX must not reconfigure, reset, or stop operator hardware by
assuming that a descriptive topology declaration grants lifecycle ownership.

## Decision

Cycle detection applies to `data_plane: graphx` edges. Descriptive
`data_plane: external` edges may participate in cycles. Port direction, schema,
transport, raw framing, address, and all other validation still applies. Raw
external edges remain non-constructible by `TransportFactory`.

The Phase 13 control edge uses TLS 1.3 with mutual certificate authentication.
Its narrow JSON-line vocabulary and responses are limited to 4,096 bytes. The
UDP sample edge is explicitly unauthenticated and provides no confidentiality,
integrity, reliability, ordering, or authorization guarantee.

The portable profile owns a simulated SDR container on an isolated Docker
bridge. The native-Linux profile owns only its disposable test namespace,
veths, OVS bridge, SPAN port, supporting containers, and simulator process. A
real SDR remains `runtime: external`, `lifecycle: external`; attaching a
physical interface is an explicit operator action outside the Phase 13 script.

## Consequences

- Physical control/data loops can be represented accurately without enabling
  cyclic execution in the GraphX-managed runtime.
- Tooling that interprets a graph as an execution DAG must filter to GraphX
  data-plane edges.
- The GUI may render an external-edge cycle because it is a network topology,
  not a GraphX scheduling plan.
- Physical attachment needs a future ownership/reconciliation capability before
  GraphX can safely mutate operator interfaces automatically.

## Rejected alternatives

- Omitting the control edge would make the displayed topology inaccurate.
- Introducing a fictitious controller node would violate the intentionally
  minimal one-SDR/one-processor example.
- Allowing all cycles would weaken a valid GraphX runtime constraint.
- Plain TCP control would leave device mutation unauthenticated.
