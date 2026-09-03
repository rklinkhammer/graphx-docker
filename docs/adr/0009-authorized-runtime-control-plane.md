# ADR 0009: Authorized runtime control plane

Status: accepted

## Context

GraphX already had a single bearer-protected pause/resume prototype transported
over the telemetry socket. It did not identify commands, scope operators to
actions or nodes, distinguish delivery from acknowledgement, expire commands,
make retries idempotent, or retain an audit trail. Collector state could
therefore claim success before a runtime had applied an action.

## Decision

The telemetry service owns a bounded command ledger and versioned local policy.
Each principal has a secret-file-backed bearer, permitted actions, and permitted
node identifiers. Policy replacement is detected atomically and malformed policy
fails closed. The original `GRAPHX_CONTROL_TOKEN` remains an explicit all-action,
all-node compatibility principal when no policy file is configured.

Every command has a UUID, actor, action, targets, optional reason, issue and
expiry times, delivery count, per-node acknowledgements, and one of `pending`,
`accepted`, `rejected`, or `timed-out`. `Idempotency-Key` is scoped to the actor;
an exact retry returns the original command and a conflicting retry is rejected.
Pause/resume is accepted only after authenticated, command-correlated runtime
acknowledgements. Reset is deliberately collector-local and completes
synchronously. Commands and audits are bounded in memory; audit metadata is also
offered to the Phase 7 history worker when history is enabled.

Runtime control uses the Phase 5 HMAC envelope. Policy mode requires a bounded
manifest assigning every topology node a unique secret; each runtime receives
only its own secret. This makes the claimed node identity cryptographically
meaningful and prevents a compromised peer from taking over another node's UDP
endpoint. The legacy single-token compatibility mode retains the shared-secret
trust domain. A source applies only an unexpired command targeted to its exact
node ID, then returns a signed acknowledgement containing command ID and state.

Reading another actor's command and reading live or durable audit are explicit
`commands:read:any` and `audit:read` permissions. Observation authorization
cannot retrieve control audit or command summaries. Optional reasons are
retained operational data and are rejected when they contain any configured
GraphX credential. Authenticated runtime fields remain untrusted: negative ACK
errors retain only a small protocol-code allowlist, and every other value is
replaced with `runtime-rejected` before reaching the ledger, APIs, audit,
history, or logs.

The collector publishes observation, control, shared-HMAC, and per-node runtime
credentials through one registry. The registry compares SHA-256 fingerprints
and rejects any value assigned to more than one role. Policy/token and runtime
manifest candidates are loaded synchronously and become usable together only
after this cross-domain check; failure clears both snapshots and live control
endpoints. Ordinary telemetry is likewise normalized once before fan-out:
event-kind property allowlists remove unknown fields, diagnostic text is
bounded, connection/backpressure state uses protocol allowlists, and active
credentials are redacted from every retained string.

Credential authentication and redaction have different transition needs.
Ordinary request authentication uses the bounded one-second poll, while a valid
signed datagram forces a synchronous combined reload after authentication and
before fan-out. This discovers a file-backed candidate before its literal value
can be retained. When a valid snapshot replaces another, removed values remain
in a redaction-only registry for 60 seconds. The registry is capped at 4,096
retired values and fails ingestion/control closed on overflow. Authentication
never accepts from this overlap: only the current validated snapshot is usable.

Because process memory does not survive a collector restart, deployments may
project a separate version-1 previous-credential manifest. It contains an
absolute expiry no more than 60 seconds ahead and bounded protected files for
old values. The collector loads these as redaction-only values before readiness;
they never enter either authenticator. This keeps crash/restart behavior safe
without creating a collector-owned durable secret journal. The deployment owns
the atomic current/previous transition and removes previous values after expiry.
Invalid transition state and reuse across different logical roles fail the
combined credential snapshot closed.

## Consequences

- Operator credentials are separate from read-only observation credentials.
- Configuration must use a distinct value for every credential role. Invalid
  startup or rotation makes the service unready and disables authenticated
  runtime ingestion and control until a valid combined snapshot is available.
- Direct credential filtering limits accidental retention but cannot prevent an
  authenticated malicious runtime from encoding information in unrelated valid
  operational values; observation and history remain sensitive.
- Each accepted signed telemetry event performs bounded credential-file I/O
  before fan-out. Telemetry is best effort and cannot block graph execution;
  deployments with very large identity manifests must capacity-test collector
  throughput. The reload and 4,096-value overlap bounds prevent unbounded work
  or memory growth.
- Least-privilege node/action policy and attributable audit are available without
  adding an identity-provider or database dependency.
- The HTTP command ledger is single-collector state; durable audit survives a
  restart when history is enabled, but pending commands are not recovered.
- Bearer files and per-node HMAC projection/rotation remain deployment concerns.
- Restart-safe rotation requires the deployment to project old values and an
  expiring manifest before replacing current files; the collector deliberately
  does not persist raw retired credentials itself.
- UDP delivery is bounded best effort. Timeout is explicit and does not imply
  cancellation; an acknowledgement received after expiry is ignored.
- This phase does not add distributed consensus, scheduling, fault injection,
  arbitrary process execution, or Phase 9 Wireshark behavior.
