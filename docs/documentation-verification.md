# Documentation verification

This report covers the customer documentation and architecture review package.
It is documentation evidence, not release certification. No workload, privileged
operation, VM lifecycle, image build or publication is part of this audit.

## Document organization

The documentation directory contains seven Markdown files. Topic pages are
consolidated into two books with contents lists and stable chapter links:

| Maintained document | Scope |
|---|---|
| [User guide](user-guide.md) | Everyday workflows plus example selection, console, CLI, configuration, execution, platform, metrics, history, capture, scenarios, QEMU, releases, manual acceptance, troubleshooting and glossary. |
| [Architecture](GraphX_Architecture.md) | System overview plus transport lifecycle, envelope and transport contracts, network ownership, control policy, security and the architecture review checklist. |
| [Index](README.md) | Entry points to the books and supporting records. |
| [Project decisions](project-decisions.md) | Accepted constraints that govern repository changes. |
| [Test procedure](test-procedure.md) | Environment selection, authorization and test-family reference. |
| [License inventory](release-license-inventory.md) | Versioned dependency and redistribution inventory. |
| This report | Documentation evidence and its limits. |

Example-specific instructions remain beside their authored graphs. Infrastructure
and guest setup remain beside their implementation sources. The duplicate
complete-system demo page is covered by the user workflow and example quick start.
Repository links target the consolidated chapters; removed topic filenames are
not compatibility redirects. External bookmarks to those filenames need updating.

The architecture review checklist is ready for use; consolidation does not claim
that the architecture review has been performed or its open items resolved.

## Current checks

Checks performed on the macOS ARM64 development host on 2026-09-14:

| Check | Result and method |
|---|---|
| Repository documentation check | Passed `python3 tests/test_documentation_consistency.py . build/dev/graphx`; checks navigation/version consistency and validates the complete user-guide YAML through the C++ loader. |
| Expanded navigation and anchors | Passed a temporary read-only audit of 36 documents and 345 local links/anchors across root customer documents and `docs/`, `examples/`, `infrastructure/`, `guests/`, `wireshark/`, including newly created files. Unlike the repository check, it checks Markdown heading fragments as well as file existence. |
| Consolidated references | Passed a repository-wide read-only check of 164 links to the consolidated books/test procedure and a deleted-path scan. The release archive documentation inventory exactly matches all seven maintained files. |
| Lima documentation contract | Passed `python3 tests/test_lima_environment.py .`; static checks and mocked command paths, without a VM or privileged workload. |
| Current example target matrix | Passed `build/dev/graphx example list --json`, compared programmatically to `examples/README.md`: 25 authored inputs and 65 accepted target pairs. This invokes normalization, not runtime execution. |
| CLI options and effects | Reviewed `build/dev/graphx --help`, `build/dev/graphx example --help`, the C++ dispatcher and Python argument parser. Validated separation of read-only, preparation, runtime and browser effects; did not execute documented mutation commands. |
| Architecture diagrams | All six Mermaid blocks parsed with Mermaid 11.12.0 and jsdom 26.1.0 using Node 24.20.0. Dependencies were installed in a temporary directory with lifecycle scripts disabled. This establishes syntax, not browser-specific SVG layout/rendering. |
| Source alignment | Reviewed authoritative schemas, loader/compiler, native/Compose ownership, network/scenario/guest modules, node bindings, envelope protocol, telemetry/session/control/history/capture modules, CLI orchestration and release recipes. Major architecture claims link to those sources and related tests. |
| Whitespace integrity | `git diff --check` passed. |

Temporary check scripts and parser dependencies are validation tooling, not new
maintained configuration authorities. The expanded link check includes new documents
that the existing Git-tracked-file-only checker does not yet enumerate.

Changes are documentation plus two filename-reference updates: the release
archive documentation allowlist and the Lima static test's documentation scan.
No runtime logic, schema, catalog, shell script, artifact receipt, credential or
ownership state was changed. Quick, portable and full profiles were not run: they
include executable fixtures/workload checks beyond the requested documentation
validation scope. No fresh Linux, Lima, OrbStack, TCG, KVM or browser runtime
acceptance is claimed. ShellCheck is not applicable because no shell scripts changed.
No external website/reference-link availability check or diagram rendering check
was performed. Local links, command contracts and parser syntax were checked.

## Prior runtime evidence and limitations

The previous [acceptance record](../design/graph-generation/p10-verification.md)
records OrbStack ARM64, Linux ARM64 Docker in Lima, OVS/scenario laboratories,
x86_64 guest execution under TCG in ARM64 Lima and Safari checks for its stated
source and 24-input inventory. It is prior evidence, not a rerun of all current
examples. The current example inventory additionally includes `sample-pipeline/ovs`.

The local `outputs/console-session/verification.json` acceptance summary
records a later OVS pipeline run in Lima and Safari: automatic login, fragment
removal, refresh, pause, reset, resume and reopening the same graph. Generated
local evidence may be absent from another checkout; it is not maintained source
or a substitute for the source-linked tests.

Native Linux x86_64 execution qualification and clean independent repeat-release
qualification remain open in the referenced acceptance record. Lima evidence does
not close native-host qualification; TCG evidence does not establish KVM. Physical
radio startup and unimplemented guest traffic contracts remain outside supported
claims. Other browsers and remote deployment arrangements were not exercised here.

See [architecture review](GraphX_Architecture.md#architecture-review-checklist) for residual risks and open questions.
