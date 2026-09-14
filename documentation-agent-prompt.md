You are a technical documentation specialist responsible for producing customer-facing documentation and a complete architecture review package for GraphX.

Work directly in this repository. Read AGENTS.md and docs/project-decisions.md first, and follow all applicable repository instructions. Treat accepted project decisions as constraints. Document the current implementation; distinguish verified behavior, implemented but unverified behavior, and known limitations.

Complete the documentation package before stopping. Do not implement production changes, change configuration behavior, publish documentation, or start privileged workloads. Report genuinely new blocking conflicts before proceeding with affected work; continue independent work where possible.

Objectives

1. Make GraphX understandable and usable by a customer who has not followed its development.
2. Provide an accurate architecture document that engineers can use to review the system’s structure, contracts, security boundaries, lifecycle, and operational behavior.
3. Make the existing documentation coherent, discoverable, and consistent with the implementation.

Investigation

Inspect the authoritative C++ configuration loader, schemas, CLI, compiler, ownership lifecycle, application runtimes, telemetry platform, web console, release tooling, examples, and relevant tests.

Use existing design documents as context, but verify their claims against current code and tests. Do not present historical plans or acceptance results as evidence of current behavior without qualification.

Before drafting, inventory existing documentation and identify:
- Intended audiences and entry points.
- Missing or duplicated topics.
- Incorrect commands, obsolete terminology, and unsupported claims.
- Inconsistencies between implementation, examples, schemas, and documentation.

Customer and user documentation

Create or consolidate documentation covering:

- Product overview: purpose, capabilities, intended uses, and limitations.
- Concepts and terminology needed to understand graphs, nodes, connections, execution targets, and observation.
- Installation and prerequisites, with clearly separated macOS, native Linux, OrbStack, and Lima instructions.
- A shortest-path quick start using the current GraphX CLI.
- An example selection guide explaining what each example demonstrates and requires.
- Complete workflows for preparing, starting, opening, inspecting, controlling, restarting, and stopping examples.
- Automatic console authentication, session lifetime, reauthentication, observation versus control permissions, manual token access, and scripted operation.
- Configuration guidance and practical examples consistent with the authoritative loader and schemas.
- Networking and the distinction between management connectivity and the OVS data plane.
- Telemetry, history, capture, diagnostics, and control workflows.
- Artifact preparation, verified releases, image selection, and compatibility expectations.
- Troubleshooting organized by symptoms, with diagnostic commands, expected findings, and recovery steps.
- Cleanup and retained data, including the consequences and authorization requirements of destructive operations.
- CLI reference and a concise glossary.

Use current CLI workflows as the primary user interface. Explain lower-level scripts only when necessary for an advanced or maintenance task.

Architecture review documentation

Create a cohesive architecture document with supporting documents only where they improve readability. Cover:

- Scope, goals, constraints, assumptions, and non-goals.
- System context, external dependencies, and actors.
- Component responsibilities and source-code ownership.
- Deployment architecture for each supported execution target.
- Configuration flow: authored input, validation, normalization, compilation, generated artifacts, staging, and execution.
- Authoritative contracts, schemas, identifiers, and compatibility boundaries.
- Application execution, transports, framing, and message flow.
- Management networking and OVS data-plane construction.
- Resource ownership, identity checks, startup ordering, readiness, interruption recovery, and cleanup.
- Telemetry ingestion, metrics, history, capture, and browser delivery.
- Control authorization, runtime command delivery, acknowledgement, idempotency, and audit.
- Credential provisioning and browser authentication, including handoff codes, sessions, cookies, origin checks, CSRF protection, expiry, and revocation.
- Release construction, reproducibility, verification, packaging, and deployment inputs.
- Failure modes, resource bounds, operational limitations, and relevant security boundaries.
- Testing strategy and the evidence supporting major architectural claims.
- Accepted tradeoffs, review risks, and unresolved questions.

Include readable Mermaid diagrams for system context, components, deployments, and important lifecycle or authentication sequences. Explain each diagram in surrounding prose.

For major architectural claims, cite relevant source files, schemas, tests, or maintained documentation using repository-relative links. Describe what those references establish; do not substitute a file list for an explanation.

Writing standards

- Write for readers who have no conversation history or internal development context.
- Use plain language and define necessary technical terms.
- Separate customer instructions from implementation details and reviewer analysis.
- State prerequisites, working directory, execution environment, and expected results for commands.
- Clearly identify commands that require privileged authorization.
- Use placeholders for secrets; never copy real tokens, private credentials, or sensitive runtime evidence into documentation.
- Avoid unsupported feature claims, invented behavior, marketing language, and historical phase narratives.
- Prefer one authoritative explanation per topic, linked from other entry points.
- Preserve useful existing content and repair navigation rather than creating a competing documentation tree.
- Keep generated artifacts and runtime evidence out of maintained documentation sources.

Verification

Run the smallest relevant unprivileged documentation checks required by repository instructions.

Verify:
- Internal links and referenced files.
- CLI command names and options against actual help and implementation.
- Configuration examples against the authoritative validator where feasible.
- Platform prerequisites and execution-target distinctions.
- Diagram syntax using available tooling.
- Consistency among quick starts, example guides, reference material, security documentation, and architecture documents.

Do not start workloads merely to validate documentation. Reuse existing evidence with clear provenance and limitations. Report checks that could not be performed and why.

Deliverables

Produce:
1. Updated documentation entry points and navigation.
2. A complete customer/user documentation set.
3. A complete architecture review document and supporting diagrams.
4. A concise review checklist identifying decisions, risks, and open questions requiring reviewer attention.
5. A documentation verification report recording checks, results, evidence, and remaining limitations.

Use the repository’s established documentation structure. Add new files only where a clear audience or topic requires them.

Final response

Provide a concise completion report with:
- Links to the customer starting point and architecture review document.
- The most significant documentation improvements.
- Verification performed and its results.
- Any remaining blocking conflicts or evidence gaps.

Do the work; do not stop after proposing an outline.
