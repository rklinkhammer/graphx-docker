# P4 verification — application availability

## Current acceptance

**P4 passes: 9/9 cases in the dedicated GraphX Lima guest, Linux ARM64.**
[P3 verification](p3-verification.md) records the complete preceding network
qualification. The [operator runbook](privileged-verification-runbook.md) gives
the authorized execution, preparation and owned recovery procedure.

Current source base: `3a120c9b2b6f8245ec26b67440ad21c8403cea12`, with the
uncommitted verification corrections. The sole VITA codec/runtime remains
vrt_framework `dbe85d37155145842da60367af1c4beef8801b0c`.

Verified private images: `outputs/verification/p301-fix/images-identity/`.
Each role passed two independent no-cache builds, archive reproducibility,
OCI inventory/SBOM checks and smoke tests. Images were built once for the final
runtime; every fixture created fresh containers. No image was published.

The aggregate machine-readable report is
`outputs/verification/p301-fix/p34-results.json`. It retains the underlying
run results, image digests, harness hashes, selected observations and final
inventory comparison. Full runtime evidence remains guest-local under
`/var/lib/graphx/verification/p301-fix/`.

## Case results

P4-01–P4-06 passed in `run-identity/`. P4-07–P4-09 passed in `run-final4/`
with the same verified image digests and CLI. Each run records its exact harness
hashes and preparation. The earlier P4-07 attempt stopped because the fault
injector supplied a UUID to the name-only `del-br` command; the successful run
uses an atomic UUID/name/ownership check and name-based deletion.

| Case | Check | Result | Seconds |
|---|---|---|---:|
| P4-01 | Available-policy baseline | PASS | 14.57 |
| P4-02 | Fourteen pre-readiness exit/stall cases | PASS | 267.41 |
| P4-03 | Invalid and unsafe startup rejection | PASS | 139.73 |
| P4-04 | Each application killed after release | PASS | 134.38 |
| P4-05 | Missing dependencies without fabricated progress | PASS | 32.25 |
| P4-06 | Recorder independence with capture off/on | PASS | 37.39 |
| P4-07 | Shared OVS bridge loss | PASS | 16.53 |
| P4-08 | Twenty mutation checkpoints and interrupted stop/restart | PASS | 178.10 |
| P4-09 | Owned cleanup | PASS | 14.60 |
## Acceptance details

The available-policy matrix exercised safe exit and readiness timeout for each
of seven applications. Failed applications stopped before release; the healthy
subset was admitted and container restart counts stayed zero. Invalid exits,
credential generation changes, foreign release tokens, MTU/filter failures,
identity substitution, invalid configuration and substituted images rejected startup.

Post-release failures of each application preserved the expected healthy paths.
Processor loss left configured radios transmitting and detector results stale;
missing startup dependencies produced no fabricated detector progress. Recorder
loss did not stop acquisition or independent diagnostic capture. Capture-off and
capture-on fixtures both passed.

The shared-bridge fault was applied only after its UUID, name, owner, graph and
configuration hash matched in the same OVS transaction. The resulting outage
was reported unhealthy, detector results became stale and containers did not
restart to conceal the outage.

Recovery covered ten injected failures and ten abrupt crashes at registered
infrastructure mutation points, including the independent diagnostic mirror.
Interrupted shutdown with a stopped recorder was followed by owned cleanup,
restart and fresh application progress. The final cleanup case passed.

Both runs preserved their independent sentinels and pre-existing resources.
The final inventory comparison found no containers or OVS bridges remaining.
Retained history/capture volumes are intentional evidence, not live workloads.

This qualifies application availability and recovery on the dedicated Lima ARM64
container path. It does not establish native Linux host, QEMU/TCG/KVM, physical
radio, published-image, P5 or P6 acceptance. The default transactional policy and
VITA protocol implementation remain unchanged.

## P4 live case specifications

The executable specification is `tests/test_vita_live.py`, cases P4-01–P4-09.
See the [runbook matrix](privileged-verification-runbook.md#6-p4-automated-case-matrix)
for selections, expected outcomes and bounded resource requirements. Use a fresh
run root and matching preparation for each invocation.
