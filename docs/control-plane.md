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
and policy modes are mutually exclusive. Direct-token mode applies only to
configurations without an instance ID.

Control results and audit records are size- and count-bounded. Credentials are
redacted from responses, logs, history, and normalized configuration.


For explicit instances, the runtime identity manifest uses version 2 with
`graph_id`, `instance_id`, and active `execution_id` values per node. A control
policy, when enabled, also uses version 2 and the matching `graph_id` and
`instance_id` alongside its existing `principals` list. A mismatched or malformed
file fails closed. An observation-only instance can omit the control policy.
See [runtime registration](runtime-lifecycle.md#instance-aware-activation).

Requests to `/api/control/commands` include `graphId`, `instanceId`, and, for
pause/resume, `targetExecutions` mapping every target node to its current execution
ID. The browser obtains these values from the snapshot. Stale instance or
execution selections receive HTTP 409. Instance control uses this JSON endpoint;
the legacy action-only shortcut cannot supply a complete instance target.

Idempotency includes the principal, instance, action, targets, execution IDs, and
reason. Reusing a key for a replacement execution is a conflict. Dispatch checks
the current registration, runtimes check the signed target tuple, and
acknowledgements must match the command's original execution. Retirement rejects
pending commands; it never redirects them to a replacement. Responses and audit
records retain the original target identities without exposing credentials.
