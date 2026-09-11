# Migration M3 remediation

**Recorded:** 2026-09-09 (America/New_York)  
**Repository:** `~/workspace/graphx-docker`  
**Source review:** `migration_m3_verification.md`  
**State:** remediated and awaiting independent re-verification

## Outcome

All four independent M3 findings are remediated. The historical verification
report remains unchanged. This record identifies the product fixes and fresh
implementation-side evidence; it does not replace the required independent
re-verification.

## Finding closure

| Finding | Remediation | Regression evidence | Result |
|---|---|---|---|
| P1 dangling state-file symlink was overwritten | State presence now uses `symlink_status` rather than a following existence check. Create checks before opening its per-graph lock and again after locking. Initial ledger publication uses atomic `link(2)` no-replace semantics, so a raced directory entry also fails with `EEXIST`. | The privileged Linux test preserves the dangling symlink and absent target, observes no lock, and proves neither declared OVS bridge was created. | Closed |
| P1 `graphx_graph` was not part of ownership | Status, rollback discovery, recovery discovery, complete-set cleanup preflight, and the atomic OVSDB delete condition now require the recorded graph marker in addition to UUID, owner token, and configuration digest. | The privileged Linux test changes only `graphx_graph`; status returns unhealthy and destroy refuses before deleting either bridge. After the marker is restored, normal destroy succeeds. | Closed |
| P2 status created state artifacts | Status now takes a separate inspection-only path. It validates a pre-existing 0700 root, opens a pre-existing 0600 regular lock with `O_RDONLY | O_NOFOLLOW`, takes a non-blocking shared lock, and reads a pre-existing ledger. It never calls directory creation, `O_CREAT`, `chmod`, `fchmod`, or state publication. | The privileged Linux test runs status against both an absent root and an existing empty root, requires exit 2, and proves neither a root nor a lock is created. Portable source contracts require the shared no-create lock path. | Closed |
| P2 architecture document described M2 behavior | The executive summary, limitation statement, and decision matrix now state that M3 realizes identity-owned OVS bridges while endpoint attachment, profile flows, and QEMU TAP remain deferred. | The maintained Markdown passed documentation consistency. The regenerated DOCX rendered to 23 pages and every page was visually inspected without clipping, overlap, missing glyphs, or broken tables. | Closed |

## Changed implementation surfaces

- `src/ownership.cpp` contains the no-follow entry inspection, inspection-only
  state-root path, no-replace initial ledger publication, shared status lock,
  and complete graph-marker checks.
- `tests/test_m3_ownership.py` prevents regression to following state checks and
  requires the graph/status source contracts.
- `tests/test_m3_ownership_live.py` exercises the filesystem and OVS findings on
  privileged Linux. `GRAPHX_ENABLE_LINUX_OVS_TESTS=ON` enables its CTest entry;
  it remains off by default for portable and unprivileged builds.
- `docs/ownership-m3.md`, `docs/security.md`, and
  `docs/GraphX_Architecture.md` describe the corrected runtime boundary.
- `docs/GraphX_Architecture.docx` is regenerated from the maintained Markdown.
- `migration_m3_handoff.md` points readers to this remediation record, while
  `migration_m3_verification.md` is preserved as the independent historical
  finding report.

## Fresh evidence

### Portable and analysis gates

- `scripts/verify.sh portable` passed both supported language configurations:
  39 of 39 C++23 tests and 39 of 39 C++20 tests, all checked-in configuration
  validation, local data-plane exercises, 77 telemetry tests, 16 web tests, and
  the production web build. Log:
  `outputs/verification/20260909T234305Z-portable.log`.
- `scripts/verify.sh quality` passed formatting for 49 C++ files, clang-tidy,
  and cppcheck. Log:
  `outputs/verification/20260909T234425Z-quality.log`.
- `scripts/verify.sh sanitizers` passed all 39 enabled tests plus the sanitizer
  coverage contract under the supported macOS LLVM 21 UBSan profile. Log:
  `outputs/verification/20260909T234446Z-sanitizers.log`.
- `git diff --check` passed.

### Lima ARM64 and system OVS

- The retained Lima instance was `graphx`, running ARM64 with the `vz` backend.
- A fresh guest build at `/var/tmp/graphx-m3-remediation-build` used GCC 13 and
  enabled `GRAPHX_ENABLE_LINUX_OVS_TESTS`.
- `graphx-m3-ownership-live` passed as guest root against system OVS. It covered
  absent-root read-only status, dangling-ledger refusal, graph-marker status
  rejection, complete-set destroy refusal, identity restoration, and exact
  cleanup.
- The retained M1 verifier passed afterward with evidence at
  `/var/lib/graphx/m1/evidence/20260909T234530Z-fdc45ca6`.
- Final guest inspection found no OVS bridges, network namespaces, or per-graph
  records under the default M3 state root.

## Residual scope

M3 still owns bridges only. It does not create OVS ports, veth pairs,
namespaces, container attachments, TAP devices, profile flows, routes, mirrors,
faults, captures, or processes. Those remain M4 and later work. Independent
re-verification should rerun the new live test and may repeat the original
adversarial cases directly before accepting M3.
