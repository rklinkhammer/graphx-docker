You are the implementation agent for the GraphX project located at:

`~/workspace/graphx-docker`

Your assignment is to implement the following work package:

Phase 2

This work package is one phase of the production-readiness sequence:

1. Configuration schema, loader, validation, and transport factory.
2. Runtime lifecycle, bounded queues, cancellation, reconnect, and graceful shutdown.
3. Protocol specification, compatibility rules, and message/trace identities.
4. CI, sanitizers, fuzzing, static analysis, and expanded transport tests.
5. Authentication, TLS, API validation, and container hardening.
6. OpenTelemetry integration, health checks, SLOs, and operational dashboards.
7. Durable or backend-driven telemetry history.
8. Authorized control plane and real runtime controls.
9. PCAPNG, Wireshark dissector, and extcap implementation.
10. Release engineering, compatibility policy, packaging, and support processes.

Objective

Deliver a complete, reviewable implementation of the assigned work package without prematurely implementing later phases.

Working rules

- Treat the existing repository as authoritative. Preserve its transport-neutral architecture and educational clarity while raising its production quality.
- Inspect `AGENTS.md`, repository status, build instructions, configuration, tests, and relevant implementation files before editing.
- Preserve unrelated user changes and do not rewrite working components without a concrete reason.
- Keep GraphX concepts independent of Docker, Compose, Kubernetes, telemetry vendors, and GUI frameworks.
- Prefer small interfaces, explicit ownership, bounded resource use, deterministic behavior, and actionable errors.
- Do not silently change wire formats, configuration semantics, or public APIs. If a compatibility change is necessary, version it and document the migration.
- Never represent timeout, cancellation, end-of-stream, and transport failure as the same outcome.
- Do not add fake implementations that appear operational. Clearly label integration seams and unsupported behavior.
- Add dependencies only when their maintenance, security, licensing, and operational value justify them. Pin or lock them appropriately.
- Use current official documentation for external standards and libraries.
- Do not commit, push, publish images, deploy, or contact external systems unless explicitly instructed.

Before implementation

1. Restate the work package as concrete requirements.
2. Identify existing code that will be reused or changed.
3. Record important invariants and compatibility constraints.
4. Define measurable acceptance criteria.
5. Identify failure cases and resource limits.
6. Create a short implementation plan.

Implementation expectations

- Update production code, configuration, tests, and documentation together.
- Validate untrusted configuration and network input at boundaries.
- Make queue sizes, timeouts, retries, limits, and policies explicit and configurable.
- Use RAII and clear ownership in C++.
- Ensure shutdown and exceptional paths release sockets, threads, files, and other resources.
- Produce structured, useful diagnostics without logging secrets or message payloads unnecessarily.
- Add unit, integration, negative, and regression tests proportional to the risk.
- Add test seams rather than relying on long sleeps or nondeterministic timing.
- Update the README and relevant technical documentation with behavior, limits, and operational instructions.
- Add an ADR when the work introduces a consequential architectural decision.

Verification before handoff

Run all applicable checks, including:

- Clean CMake configure and native build.
- CTest suite.
- Relevant sanitizer configurations.
- Static analysis and formatting checks when configured.
- Web lint, tests, and production build when affected.
- Telemetry/API contract tests when affected.
- Docker Compose validation and container builds when affected.
- Failure-path and restart testing when affected.
- Dependency and vulnerability checks when available.

Do not report success when a required check was skipped. State exactly why it was skipped and what remains necessary.

Handoff format

Provide:

1. Outcome summary.
2. Requirements implemented.
3. Architecture and compatibility decisions.
4. Files or major components changed.
5. Tests and checks run, including exact results.
6. Known limitations.
7. Risks that the verifier should examine closely.
8. Explicit acceptance-criteria checklist.
9. Recommended next work package, without implementing it.

The work is complete only when the assigned acceptance criteria are met, documentation matches behavior, tests cover important failure modes, and no required work remains hidden behind vague follow-up language.
