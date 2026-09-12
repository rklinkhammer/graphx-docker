# Control plane

The telemetry service exposes bounded pause, resume, and reset actions for
nodes that declare GraphX or origin control. Requests require bearer authentication
and authorization for the action and node. When an Origin header is present, it
must match the same origin or the configured allowlist. An optional
`Idempotency-Key` header provides bounded replay detection; the browser supplies
one for each command. Fault injection uses the privileged Linux infrastructure
CLI on native Linux or inside Lima, not the telemetry control API.

Production policy mode uses `GRAPHX_CONTROL_POLICY_FILE` together with
`GRAPHX_RUNTIME_IDENTITY_FILE`. A policy names principals, token files, permissions,
and allowed nodes. Runtime identities bind commands to the process or QMP endpoint
that may execute them. Files are bounded, permission-checked, and re-read for safe
credential rotation.

`GRAPHX_CONTROL_TOKEN` is a direct-token mode for the portable demo. It grants the
single `direct-operator` principal all configured-node actions and requires
`GRAPHX_TELEMETRY_SHARED_SECRET` for runtime command authentication. Direct-token
and policy modes are mutually exclusive.

Control results and audit records are size- and count-bounded. Credentials are
redacted from responses, logs, history, and normalized configuration.
