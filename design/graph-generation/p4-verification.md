# P4 shared packaging verification

P4 provides one runtime, telemetry and SDR software image recipe, reusable with
the two fixed catalog templates. It removes the sample graph from image/native
packaging, the default generator entrypoint, per-graph Compose build sections,
and the example-owned SDR Dockerfile. SDR application names now match the catalog;
the image does not copy topology-specific packet-observation code. All shared
images use UID/GID 65532 and explicit commands.

## Release pin contract

`scripts/release/image_release.py` builds each image twice, verifies every OCI
descriptor and layer, and emits deterministic OCI archives with real manifest
digests. These differ from Docker image config IDs. The final archives are imported
and smoke-tested with no network, a read-only root filesystem, no capabilities
and the declared user. No graph is started. The source catalog remains the
unverified development input; the generated release catalog changes image pins,
container type revisions and lock hashes together. The two fixed template
contracts remain unchanged. Generated files under `outputs/` are evidence, not
maintained source.

The release-specific catalog contains architecture and archive/config/manifest
identities. Its references identify offline OCI artifacts, not images claimed
to be available from a registry. Local dirty candidates are explicitly labeled;
no GitHub or registry release was published. Independent verification compares
archive checksums, every layer, installed file inventory, SPDX contents and catalog
lock hashes. These checks establish content integrity, not publisher authenticity.
The native release retains its independent archive/checksum/SPDX verifier.

Debian packages use the dated signature-verified snapshot in `docker/debian.sources`.
All base images are digest-pinned; npm uses its lockfiles. Canonical OCI packaging
normalizes timestamps and tar metadata after image-store export while preserving
file bytes, modes, owners, links, xattrs and layer order. This derives a new image
identity from the actual normalized bytes. Both uncached builds must produce the
same complete archive, manifest and file/package inventories.

The bounded layer scan rejects embedded authored graphs, credential directories
and PEM private-key payloads. The exact public GnuTLS known-answer keys from
[upstream self-tests](https://github.com/gnutls/gnutls/blob/3.7.9/lib/crypto-selftests-pk.c)
are recognized by their full PEM hashes only inside `libgnutls`. The check is not
a universal detector for every secret encoding. SPDX records include installed
OS packages, language runtimes, production Node modules, bundled yaml-cpp/OpenSSL
build metadata and the console's production dependencies; unresolved licenses
remain `NOASSERTION`.

The release workflow includes SDR in staging, provenance attestation, SPDX
validation, immutable promotion and compensating cleanup. CI selects shared
recipes explicitly and runs offline image reproducibility checks. No publication
workflow was invoked for this implementation.

## Evidence

Verification completed on 2026-09-13. Host: native macOS ARM64. Selected engine:
OrbStack, Docker 29.4.0, Compose 5.1.2, Buildx 0.33.0; image platform: Linux ARM64.
`docker info`, `docker context show` and `docker compose version` passed before
image checks. No privileged builder was created.

| Check | Result | Retained evidence |
|---|---|---|
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick` | 33/33 passed | `outputs/verification/20260913T165242Z-quick.log` |
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality` | Formatting, clang-tidy and cppcheck passed | `outputs/verification/20260913T165312Z-quality.log` |
| `PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable` | 33 CTests, 32 execution gates, 92 telemetry tests, 17 web tests and console build passed | `outputs/verification/20260913T165456Z-portable.log` |
| Shared OCI image build, repeat and smoke tests | All three images repeat byte-for-byte across uncached builds; imported artifacts pass six bounded smoke commands | `outputs/verification/p4/shared-images.log`, `identity-smoke.log` |
| Independent image verification | Archive checksums, manifest/config/layer identities, installed files, SPDX inventories and catalog hashes passed | `outputs/verification/p4/image-verification.log` |
| Native release build and independent verification | 35/35 CTests, installed consumer, package inventory/modes, checksums and SPDX passed | `outputs/verification/p4/native-release.log`, `native-verification.log` |
| Independent native rebuild | Complete native archive repeats byte-for-byte from a second build directory | `outputs/verification/p4/native-repeat.log` |
| S01/T01/T02 with release catalog | Three compilations and Compose validations passed; shared verified image references; zero generated Dockerfiles | `outputs/verification/p4/release-catalog-compile.log` |
| Focused offline image tests | Tampering, wrong user/architecture, traversal, embedded graphs/keys, SPDX mismatch, catalog preservation, revision overflow, timestamp normalization and owned-container cleanup after client timeout passed | `tests/test_image_release.py` |
| Revision schema tests | Positive revisions 2 and 2147483647 accepted; zero, negative, overflow, boolean, fractional and string revisions rejected | `tests/test_config_v3_matrix.py` |
| Workflow/static checks | Both workflow YAML files parse with unique step IDs; Python syntax and `git diff --check` pass | Current working tree |

No shell scripts changed, so ShellCheck is not applicable. The revision range is
aligned in authored type and normalized node schemas. Existing compiler goldens
remain unchanged because the source catalog still uses revision 1.
Final focused documentation, image-release and release-contract checks are in
`outputs/verification/p4/final-focused.log`. Smoke cleanup uses the exact returned
container ID; attached-client timeout does not leave the owned container running.

The exact release commands were:

```sh
python3 scripts/release/image_release.py build \
  --output outputs/p4-release --platform linux/arm64 --allow-dirty --no-cache
python3 scripts/release/image_release.py verify outputs/p4-release
python3 tests/test_image_release.py . build/dev/graphx outputs/p4-release
PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 scripts/release/build_release.py \
  --build-dir build/p4-package --output-dir outputs/p4-native --allow-dirty
PATH=/opt/homebrew/opt/node@24/bin:$PATH python3 scripts/release/build_release.py \
  --build-dir build/p4-package-repeat --output-dir outputs/p4-native-repeat --allow-dirty
python3 scripts/release/verify_release.py outputs/p4-native --source .
```

The offline image release is under [outputs/p4-release](../../outputs/p4-release):
three OCI archives, three SPDX files, `images.json`, the derived `catalog/`, and
the concrete S01/T01/T02 compiler outputs under `compiled-examples/`.

| Artifact | Actual OCI manifest digest | SPDX package records |
|---|---|---|
| Runtime | `sha256:a44da7bdc30bcf28003d3b886f83636c73232483c8305e77c2a0a662d667619a` | 106 |
| Telemetry | `sha256:644381e262f90dccb9b89e3927ef43a4e143a63d90da36a2f0ea44ca4664a784` | 197 |
| SDR | `sha256:4ddb5355bdd7f56b89e84953fe0cfa97bb94f328ba8cef6b33a5b1d767c05cfe` | 31 |

Counts include the root GraphX image record. Archive checksums are in `images.json`.
The native archive at
[outputs/p4-native/graphx-1.1.0-darwin-aarch64.tar.gz](../../outputs/p4-native/graphx-1.1.0-darwin-aarch64.tar.gz)
has SHA-256 `249b75da2e9500d0fb6686f4ff83c952a32b3cf60c46a722693878d12e1a0aea`.
The second build produced that same complete archive hash. These are local dirty
candidates, not published releases or evidence of Linux native packaging.

Before testing, telemetry container `058868d98ccb` was running. During the session
the user intentionally killed the existing containers and removed old images;
Docker recorded SIGKILL at 16:32:23 UTC and no OOM kill. That user-directed cleanup
was left intact. The final inventory in `outputs/verification/p4/containers-after.txt`
contains only the three pre-existing stopped containers; no verification container
remains. `temporary-image-tags-after.txt` is empty. Build caches and release
artifacts are retained; no broad Docker cleanup was run by the verifier.

## Remaining phase boundaries

The compiler still reports execution unavailable and does not trust arbitrary
catalog claims as proof that artifacts exist. P5 supplies the resolved platform
configuration adapter and native Node/web packaging; P6–P9 supply graph execution,
infrastructure, guests and scenario actions. Existing normalization-at-startup
services and source overlays remain gated until those replacement contracts pass.
The packet observer belongs to the infrastructure lifecycle. P8 guest artifacts
remain unverified. No native Linux/Lima workload, OVS/veth/TAP mutation, guest
build, TCG boot or KVM evidence is claimed.
