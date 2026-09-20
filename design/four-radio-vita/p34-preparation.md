# P3/P4 qualification preparation

## Current status

Private Linux ARM64 images and the guarded live harness are verified, and all
18 privileged cases pass in the dedicated GraphX Lima guest. See
[P3 verification](p3-verification.md), [P4 verification](p4-verification.md) and the
[operator runbook](privileged-verification-runbook.md).

Source base: `3a120c9b2b6f8245ec26b67440ad21c8403cea12`, with uncommitted
verification corrections. These are dirty private candidates; the base commit
alone does not identify their bytes. No images were published. The VRT pin remains
`dbe85d37155145842da60367af1c4beef8801b0c`.

## Verified artifacts

Host images: `outputs/verification/p301-fix/images-identity/`.
Guest images: `/var/lib/graphx/verification/p301-fix/images-identity/`.
The release includes OCI archives, SPDX inventories, `images.json` and its locked
catalog. Each role passed two independent no-cache builds, canonical archive
comparison, inventory verification and bounded no-network/no-capability smoke tests.

| Role | Verified OCI manifest digest |
|---|---|
| runtime | `sha256:44f5ab4abd7b8ea3ac9355861081272ceb50e2d91e8bf0c674f6033054f165d8` |
| sdr | `sha256:ac7dd145d98d62ad4b8226d1ba94a3a58274d60e0bd4be26529d40268bd61ee5` |
| telemetry | `sha256:4c7bf2bb41960e52916a261b66e0aab4ca7cb3c516da7f85b1cc961874457569` |
| vita | `sha256:afbff9828d7c11a79c6c9718bb1869cc5a8fcd50fa824cb5dd65ae6136d8fa08` |

Host preparations are `outputs/verification/p301-fix/prepared-identity/` and
`prepared-final4/`. Matching guest directories sit under
`/var/lib/graphx/verification/p301-fix/`. Each preparation records graph IDs,
subnet, run root, image digests, extracted CLI checksum and harness-source hashes.
The live runner rejects changed inputs. Preparation itself creates no runtime
resources and grants no privileged authorization.

`run-identity/` contains all nine P3 passes and P4-01–P4-06 passes.
`run-final4/` contains P4-07–P4-09 passes using the same images and CLI with the
corrected bridge fault injector. The aggregate report is
`outputs/verification/p301-fix/p34-results.json`; it retains both underlying
run records. Fresh containers were used for every fixture. Final live inventory
matched its pre-run state, with no containers or OVS bridges remaining.

## Reproduction boundaries

Use the runbook to build fresh images once per verification run, prepare an
absent run root, check VM identity and stage the verified artifacts. Do not reuse
completed run roots. `--with-vita --qualification-hooks` enables private qualification hooks; normal
builds leave them off. The hooks inject faults in the actual applications and
pause the exact infrastructure runner child at registered mutation checkpoints.

The current user authorized the full matrix. A new operator session without
that authorization must obtain it before privileged execution. Runtime state,
logs and captures stay under `/var/lib/graphx`; cleanup uses ownership identities.
Do not reset the VM or prune global resources to bypass a failed test.

This preparation supports Lima ARM64 qualification. It does not constitute P5
published images, P6 delivery or native Linux/QEMU/TCG/KVM/physical-radio evidence.

## Supporting verification

| Check | Result | Evidence under `outputs/verification/p301-fix/` |
|---|---|---|
| Focused ownership/resource/network/harness CTests | 4/4 passed | `identity-tests.log` |
| Quick suite | 41/41 passed | `quick-identity.log` |
| Formatting, clang-tidy and cppcheck | Passed | `quality-identity.log` |
| Portable acceptance | Passed, 158 seconds | `portable-matrix.log` |
| Guest offline harness regressions | Passed | `live-final4.log` |
| Documentation consistency | Passed | `documentation-matrix.log` |
| Final inventory and all 18 privileged cases | Passed | `p34-results.json` |

No shell scripts were changed during these corrections. No native Linux host,
TCG or KVM run is included in this evidence.
