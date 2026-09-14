# Node console verification

Verified on 2026-09-14. The user explicitly authorized isolated privileged Lima
acceptance after portable verification passed. Guest login remains supplied by
explicitly selected images; these results do not certify an interactive shell.

## Portable and static checks

Commands run from the repository root on macOS ARM64:

```sh
PATH=/opt/homebrew/opt/node@24/bin:/opt/homebrew/bin:$PATH scripts/verify.sh portable
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:/opt/homebrew/bin:$PATH node --test apps/telemetry/node-console.test.mjs
/opt/homebrew/bin/python3 tests/test_image_release.py .
PATH=/opt/homebrew/opt/node@24/bin:/opt/homebrew/bin:$PATH python3 tests/test_documentation_consistency.py . build/dev/graphx
git diff --check
```

- Portable passed in 122 seconds: 41/41 CTests, 114/114 telemetry tests,
  23/23 web tests, production web build and native lifecycle acceptance with
  authenticated, nonempty node logs. Evidence:
  `outputs/verification/20260914T225138Z-portable.log`.
- Quality passed in 34 seconds: clang-format 21, clang-tidy and cppcheck.
  Evidence: `outputs/verification/20260914T222359Z-quality.log`. Subsequent changes
  affected packaging, Python acceptance assertions and documentation, not C++.
- Focused console tests passed: directory and socket substitution, malformed
  snapshots, exact Docker identity, observation/control separation, exclusive
  writer, expiry/revocation/session deadline, input bounds and backpressure.
- Web tests cover byte cursors, split UTF-8, replay/gaps, safe text rendering,
  denied access and node-selection request cancellation using a mounted panel.
- Compiler verification passed 63 target packages, repeatable output, five updated
  goldens, 15 negative cases and 33 unsupported combinations.
- Documentation consistency and a local-link scan passed (37 documents,
  351 links). No shell scripts changed; ShellCheck was not applicable.

## Linux build and live Lima acceptance

The existing identity-checked GraphX Lima VM is ARM64. The native release passed
43/43 Linux CTests. Shared OCI images and both guest artifacts passed their release
verifiers. Artifacts are development candidates built with `--allow-dirty`, not
published releases. Guest builds used the supported `--work-dir` option with an
isolated copy of the existing Buildroot cache.

The final acceptance commands ran inside Lima, from the staged acceptance source:

```sh
cd /var/lib/graphx/examples/artifacts/17e80083201ddda08ba1/acceptance-source
artifacts=/var/lib/graphx/examples/artifacts/17e80083201ddda08ba1
sudo python3 tests/test_guest_execution_live.py \
  --allow-privileged --target lima --case S15 \
  --release "$artifacts/guest-release-fixed" --images "$artifacts/images-fixed" \
  --guests "$artifacts/guests-fixed" \
  --output /var/lib/graphx/runs/node-console-s15-final-20260914 \
  --node-console --scenario
sudo python3 tests/test_guest_execution_live.py \
  --allow-privileged --target lima --case T03 \
  --release "$artifacts/guest-release-fixed" --images "$artifacts/images-fixed" \
  --guests "$artifacts/guests-fixed" \
  --output /var/lib/graphx/runs/node-console-t03-final-20260914 --node-console
```

Both final commands passed with cleanup return code zero. Small result summaries
were copied to `outputs/node-console/s15-lima-results.json` and
`outputs/node-console/t03-lima-results.json`; detailed evidence remains in Lima.
The isolated Buildroot cache copy was removed after verification.

S15 covers actual x86-64 guest boot under TCG, dedicated UID 65532, framed readiness,
namespace/guest TCP and UDP traffic, VLAN isolation, SPAN capture and local QMP.
T03 covers actual radio guest traffic alongside container radio, processor and sink
nodes, including mutually authenticated SDR control.

Both cases require fresh, running, nonempty, bounded log snapshots for every managed
application node. Serial checks establish the ttyS1 socket connection, reject an
observation token for input, acquire one writer, reject a second writer and stale
generation, acknowledge bounded input, and release the writer. No login prompt or
command execution inside the guest is asserted.

Each run records `results.json`, before/after inventories, startup/shutdown logs and
guest boot evidence under its guest-local output directory. Cleanup must return
zero and preserve the pre-existing container, network, bridge, namespace, link and
owned-process inventories. History volumes and evidence are intentionally retained.

Acceptance caught a missing telemetry-image module; the Dockerfile now includes
`node-console.mjs`, and image verification rejects its absence. Release packaging
also exposed the broad documentation install glob; the explicit install list now
matches the release inventory while leaving workspace review documents untouched.

## Limits of the evidence

Native macOS execution and Linux ARM64 execution inside Lima were exercised.
There was no separate bare-metal Linux or KVM run. Actual x86-64 guests ran under
TCG. Mounted web-component tests and HTTP acceptance passed; no fresh manual browser
or custom-image ttyS1 login acceptance is claimed. A custom guest login service
requires its own image-specific acceptance.
