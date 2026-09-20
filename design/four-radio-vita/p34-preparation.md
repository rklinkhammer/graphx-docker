# P3/P4 qualification preparation

## Status and scope

Private Linux ARM64 images, offline preparation and the guarded four-radio live
harness are implemented. Diagnostic capture now uses an independent host mirror
endpoint in the common ownership lifecycle. **No privileged acceptance has run;
P3/P4 remain open.** Use the [operator runbook](privileged-verification-runbook.md)
for staging, authorization, execution and recovery.

Source base: `5e869560703e05ec3764b3485c0f089d013b8b8d`, with the current uncommitted preparation changes.
The images are explicitly **dirty candidates**; the base commit alone does not
identify their bytes. The OCI manifests and executable hashes below identify the
verified candidate. `preparation.json` additionally hashes the harness and its
release-verifier sources; the live runner refuses changed code, image selection,
subnet, run root or CLI bytes. Nothing was committed, pushed, deployed or published.
The VRT pin remains `dbe85d37155145842da60367af1c4beef8801b0c`.

## Verified artifacts

Host image directory: `outputs/verification/p34-preparation/images-acceptance/`.
Host review/CLI directory: `outputs/verification/p34-preparation/prepared-reviewed/`.
Review-only Lima compiler projections (17 artifacts each) are in
`outputs/verification/p34-preparation/review-P3-compiled/` and
`outputs/verification/p34-preparation/review-P4-compiled/`. They use Mac-side paths;
the live harness recompiles on the guest. Neither directory has been staged on Lima. Guest paths reserved by the review are
`/var/lib/graphx/verification/p34/{images,prepared,run}`.

| Image role | OCI manifest digest |
|---|---|
| runtime | `sha256:fc10b724bda0f11034b76a5b92d93771c260e9efc2564f90d5cc60b29ca5eb2c` |
| sdr | `sha256:43892228600c3e78c03d1acccc26e3369832ff7c861d8a6f467fe40f808ad1a9` |
| telemetry | `sha256:2080676630484ff694dea00cc53eb69a643a59fcb8f423f18fc083736812ae97` |
| vita | `sha256:a83a2ef0a972d0abb570618add1cdde61aa7e89fb0775609de94b63adac4a6b5` |

| Installed executable | SHA-256 |
|---|---|
| graphx | `0f5c414aec12b1740ded51204522d77e53a0e504de50b23fea93d04bf72ee7b8` |
| graphx-vita-radio | `f917c90721b19c4d3f7acc0f5c138e24ca189e28cc27544171b10ea66cdc67fa` |
| graphx-vita-processor | `8c81a0b9b466b318a541ff75df2a794459201201da5eb826afaf076175dfc713` |
| graphx-vita-detector | `9bad79e092d962472589576632525a6b55c89c74fe9c0f69a8dd1ac66d553ff9` |
| graphx-vita-recorder | `98889270ecfa5d626615d16442441eafcdf9365ebb4ff49c379f6de23f637e48` |

`images.json` records config digests, archive checksums, installed files and package
inventories; per-role SPDX files and catalog locks are independently rechecked.
VITA metadata verifies the exact vrt_framework/SoapySDR revisions and includes
both licenses and the dependency SBOM. The optional VITA role does not change
ordinary three-role builds or constitute P5 publication.

## Executed commands and results

Environment: native macOS arm64; Docker context `orbstack`, Linux/aarch64 engine,
Docker Compose v5.1.2. Commands below ran from the repository root.

```sh
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration \
  -R 'graphx-(vita|ownership-state|resource-modules)' --output-on-failure
python3 tests/test_vita_live_contract.py build/vita-migration
python3 tests/test_image_release.py .
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
python3 scripts/release/image_release.py build --with-vita --allow-dirty \
  --platform linux/arm64 \
  --output outputs/verification/p34-preparation/images-acceptance
python3 tests/test_vita_live.py --prepare \
  --images outputs/verification/p34-preparation/images-acceptance \
  --output outputs/verification/p34-preparation/prepared-reviewed \
  --run-root /var/lib/graphx/verification/p34/run
python3 tests/test_documentation_consistency.py . build/dev/graphx
git diff --check
```

- Native VITA/ownership/resource regression: **23/23 passed**, 118.38 seconds.
  Includes unchanged start-epoch assertions, four-radio/P2 acceptance and P4 fault
  regressions, plus the new offline live-harness contract test.
- Quick: **41/41 passed**, 111 seconds including build.
- Quality: **passed**, 39 seconds; formatting, clang-tidy and cppcheck.
- Portable: **passed**, 156 seconds; 41 development tests, normalized consumers,
  HTTP/web and native transactional/available execution.
- Independent offline tests: **passed**. Cases include missing VITA executables,
  wrong dependency pins, derived catalog revisions, unauthorized live invocation,
  all fixture variants including sentinel, malformed/truncated Ethernet/VLAN/IP,
  unchanged counters and changed infrastructure inventories.
- Private image build: **passed**. Two cache-eligible builds per role, independent
  OCI/layer/package/SBOM/catalog verification and bounded unprivileged smoke tests.
  No claim of no-cache reproducibility is made. The Linux recorder test exercises
  receive-descriptor restrictions, TSYNC denial and a jumbo receive without NET_RAW.
  Recorder startup without NET_RAW is expected to fail closed; this is not live
  AF_PACKET, tc, namespace or OVS qualification.
- Offline extraction/preparation: **passed**, with exact CLI hash and reviewable
  authored configurations. No runtime resources were created by `--prepare`.
- Documentation consistency and whitespace: **passed**. No shell scripts changed.

Repository gate logs:
`outputs/verification/20260920T134509Z-{quick,quality}.log` and
`outputs/verification/20260920T134702Z-portable.log`.
The preparation image directories contain the independently verified artifact
records; current harness details are in `prepared-reviewed/preparation.json`.

The reviewed baseline projections were compiled with:

```sh
build/vita-migration/graphx compile outputs/verification/p34-preparation/prepared-reviewed/authored/P3-01/graph/graphx.yml --target lima --catalog-root outputs/verification/p34-preparation/prepared-reviewed/catalog --source-root outputs/verification/p34-preparation/prepared-reviewed --credential-root outputs/verification/p34-preparation/review-credentials --output outputs/verification/p34-preparation/review-P3-compiled
build/vita-migration/graphx compile outputs/verification/p34-preparation/prepared-reviewed/authored/P4-01/graph/graphx.yml --target lima --catalog-root outputs/verification/p34-preparation/prepared-reviewed/catalog --source-root outputs/verification/p34-preparation/prepared-reviewed --credential-root outputs/verification/p34-preparation/review-credentials --output outputs/verification/p34-preparation/review-P4-compiled
```

Both passed; compiling does not create runtime resources.

## Environment prerequisite failure

The read-only checks executed were:

```sh
limactl list graphx
bash -c 'source infrastructure/lima/common.sh; graphx_lima_require_host; graphx_lima_assert_identity "$(graphx_lima_digest)"'
```

**Failed prerequisite:** the instance is stopped and its fingerprint is stale.
Recorded: `ac49027a1cbafa52b3565d32a4154fe3ecf3a3aae0ae4a1760782f79da0e5791`.
Expected: `995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e`.
The identity guard was not bypassed. No VM start, provisioning, deletion or
replacement was attempted. Guest Docker/OVS readiness, PyYAML and `ethtool`/capture
tool availability are **not checked**, rather than presumed present or missing.
Request a separate environment-recovery proposal before authorizing a live run.

## Live evidence still required

`tests/test_vita_live.py --list-cases` lists P3-01–P3-09 and P4-01–P4-09.
The harness creates unique deterministic identities, compiles authored v3 inputs,
runs actual applications through the common lifecycle, checks an independent
sentinel, injects scoped faults and attempts owned cleanup. It has never executed
on this OVS path; portable tests do not establish correctness of all live commands
or timing assumptions. A failed assertion must be investigated, not relaxed to
make the report green.

In particular, acceptance must still establish actual NET_RAW setup/capability
drop and passive tc enforcement; complete jumbo/mirror bytes and duplicate behavior;
fresh PCAPNG packets after recorder death; sustained bounded recorder reception;
all live application admission/runtime failures; and interruption/recovery with
unchanged unrelated workloads. The nominal IQ source payload is **16 MB/s**.
The harness records wire counters/durations and best-effort recorder observations
separately; no measured traffic or lossless-reception claim exists yet.

Native Linux, privileged Lima, TCG and KVM remain **unrun**. TCG/KVM are outside
this container-only qualification. Sanitizer/fuzz gates were not rerun as additional
coverage for this preparation; no new protocol codec was introduced. Existing
P1/P2 evidence remains separate from the new live adapter/ownership assertions.

P3/P4 cannot close from image smoke tests, the portable suite, a successful
`run up`, or this preparation record. Closure requires the complete live matrices
and cleanup evidence in the runbook after explicit authorization.
