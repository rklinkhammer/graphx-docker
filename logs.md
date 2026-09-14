Implement node logs and interactive QEMU serial access in the GraphX web console.

Read AGENTS.md and docs/project-decisions.md first. Inspect the current implementation and follow the accepted architecture, ownership, authentication and verification contracts. Complete the implementation, focused tests and documentation. Report genuinely new blocking conflicts before proceeding with affected work; continue independent work where possible.

Scope

1. Logs for all managed node types: native processes, containers, namespace applications and QEMU guests.
2. Interactive serial access for QEMU nodes only.
3. Support any running authored graph, including supplied examples and user-defined systems.

External devices may expose logs only through an explicitly supported source. Show an honest unavailable state when no source exists. Do not infer device logs from captured traffic.

Do not add container shells, native host terminals, arbitrary host-command execution or a browser-facing QMP console.

Investigation and design

Before changing behavior, inspect:
- Current CLI log retrieval and following.
- Native, container, namespace and QEMU output handling.
- QEMU serial configuration, boot output, readiness detection and guest login support.
- Ownership ledgers, process identities, locks and cleanup.
- Platform API authentication, browser sessions, control authorization and audit.
- Web node selection and existing panels.
- Release packaging, schemas and relevant tests.

Produce a concise design under design/node-console/ describing the API, data flow, permissions, lifecycle, resource bounds and implementation sequence. Clearly distinguish existing behavior from proposed changes. Proceed with implementation once existing contracts resolve the design; ask only about genuinely blocking choices.

Logs

Add a node Console panel with a Logs view that supports:
- Bounded recent output followed by live streaming.
- Source identification, distinguishing application stdout/stderr, guest serial output and QEMU process errors where available.
- Graph, node and runtime-generation identity.
- Pause scrolling, search within loaded output and bounded download.
- Clear connecting, disconnected, stopped, unavailable, truncated and permission-denied states.
- Explicit gaps when rotation, retention or reconnect prevents complete replay.

Reuse existing log collection and ownership mechanisms where practical. Avoid duplicate collectors and unbounded storage. Define retention, cursor/replay behavior, per-node buffering, connection limits, slow-client handling and maximum message/download sizes.

Preserve boot output from guest startup, before a browser connects. Preserve bytes correctly across stream chunk boundaries. Render logs safely as text; terminal escape sequences must not become executable HTML or browser actions.

QEMU serial

Add a Serial view for interactive guest console access:
- Use a dedicated owned serial endpoint inside native Linux or the GraphX Lima guest.
- Preserve existing readiness detection and boot-log collection.
- Coordinate a single underlying serial reader where required, distributing output to logs and viewers.
- Permit multiple read-only viewers and at most one authorized input writer per guest.
- Show writer ownership and provide explicit acquire/release actions.
- Disconnect, expiry and browser closure release the writer without stopping the guest.
- Reject stale sessions after node restart or identity changes.
- Define reconnect, broker failure and guest-stop behavior.

A serial connection does not itself provide a guest shell. Inspect the guest images and explicitly document whether they provide a serial login service. If guest changes are needed, use the existing guest build and verified-artifact pipeline. Do not introduce an unauthenticated privileged shell or undocumented default credentials. Report a blocking guest authentication-policy choice if existing decisions do not resolve it.

Keep QMP separate from serial access. Do not expose Docker sockets, QMP sockets, arbitrary filesystem paths or privileged host interfaces to the browser or forward privileged sockets to macOS.

Authorization and security

Reuse existing observation authentication for log reading where consistent with the current contract. Add an explicit, node-scoped permission for interactive serial access; pause/resume/reset grants must not imply terminal access.

Integrate with the current CLI credential provisioning and automatic browser-session workflow. Do not introduce a parallel token system.

For HTTP or streaming connections:
- Apply authentication, authorization, origin and CSRF protections appropriate to the transport.
- Define expiry and revocation behavior for already-open connections.
- Keep credentials out of URLs, logs and audit payloads.
- Validate graph/node/generation identity and constrain all resource access server-side.
- Bound input rate, frame size, sessions, buffers and idle/session lifetime.

Audit connection lifecycle, writer acquisition/release and denied access. Do not record typed input by default, since it may contain passwords. Treat guest output as potentially sensitive.

Implementation constraints

- Keep the C++ loader authoritative.
- Extend schemas and normalized contracts only when required; update all consumers and fixtures together.
- Use the existing ownership lifecycle and verified compilation/release paths.
- Keep infrastructure operations within the documented Linux/Lima boundary.
- Avoid adding a new service unless the design demonstrates why existing components cannot safely own the connection.
- If a terminal-rendering dependency is introduced, justify it and update lockfiles, licensing and release inventories.
- Preserve public CLI behavior, application readiness and ordinary graph startup/stop.

Verification

Start with focused tests for:
- Log retrieval, follow, reconnect, rotation, truncation and bounded downloads.
- Missing sources and stopped/restarted nodes.
- Cross-graph/node access, path traversal, stale identities and permission failures.
- Concurrent serial viewers and exclusive writer ownership.
- Expiry, revocation, origin/CSRF checks and malformed/oversized input.
- Slow readers, stalled connections and resource cleanup.
- Guest output appearing in both logs and serial without disrupting readiness.
- Browser handling of unavailable endpoints, stream loss and denied access.

Run repository-required checks appropriate to the changed surfaces. Run ShellCheck on touched shell scripts and report exact commands.

Do not start privileged acceptance without explicit authorization for this work. Complete portable and mocked coverage first; if privileged acceptance is required, present the concrete test scope and request authorization. Report native Linux, Lima, actual TCG guest execution and KVM evidence separately. Do not claim an interactive guest test from a mocked socket or TAP lifecycle test.

Documentation and completion

Update docs/user-guide.md and docs/GraphX_Architecture.md rather than creating additional topic guides in docs/. Update relevant example instructions only where their behavior changes.

Document:
- Logs and Serial workflows.
- Permission provisioning and automatic authentication.
- Supported node sources and limitations.
- Guest login prerequisites.
- Retention, disconnect/reconnect and troubleshooting.
- Architecture, ownership and security boundaries.

Provide a verification record distinguishing tests actually run from pending acceptance.

In the final response, summarize implemented behavior, changed contracts, verification results and remaining limitations. Do not claim completion of any acceptance that was not performed.
