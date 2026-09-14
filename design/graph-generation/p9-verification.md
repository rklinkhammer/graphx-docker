# P9 scenario verification

P9 implements explicitly selected scenario actions and pre-start laboratory
selection. Native credential rotation and V04 OrbStack acceptance passed. The
S11/S12/S14/S15 privileged P9 acceptance cases passed in the ARM64 GraphX Lima VM
with explicit authorization. S15 booted an actual x86_64 TCG guest.
Physical-device startup remains gated.

## Implemented contract

- `graphx scenario plan|run|status|clear` selects a closed action by declared ID.
  The loader validates operation-specific fields and graph/credential references.
  The runner checks immutable compilation hashes, the ready graph identity and
  the common exclusive ownership lock. No action runs during `run up`.
- Action intents and outcomes use the existing ownership ledger. Pending actions
  refuse replay; common graph shutdown/restart is the recovery boundary. Manual
  routes require absence before apply and exact recorded route identity before clear.
- Timed faults reuse the common netem lifecycle, refuse pre-existing custom qdiscs,
  and register timer identity durably before releasing the child timer. A lost
  registration gate cannot leave an unrecorded active timer.
- Credential rotation reuses the generation publisher and bounded overlap reader.
  Native roots have recorded directory identity; Compose rotation uses the verified
  platform image, an owned temporary container, no network or capabilities, and the
  owned credential volume. No secret appears in an action plan, log or argv.
- Traffic checks resolve diagnostic UDP probes and expected receipt/drop from the
  graph. Guest checks use its existing unicast TCP/UDP contract, VLAN isolation,
  capture and identity-checked QMP pause/resume.
- The approved `compile --laboratory laboratory-radio` selection substitutes the
  declared SDR simulator and test credentials from the same authored graph. Its
  isolated owned bridge has no physical attachment. The original physical graph
  and external trust files remain unchanged. The compilation records the selection.
- S15's approved expectations exclude broadcast echo/multicast reception, which are
  outside the current P8 guest application contract. Manual-route wrappers now
  dispatch declared actions through the common CLI. The replacement's privileged acceptance gate is satisfied; superseded external
  launcher deletion remains part of the final conversion/removal work.

## Evidence

| Check | Result |
|---|---|
| Scenario configuration/selection tests | Passed unknown/missing/inapplicable fields, unknown references, duplicate IDs, plan tampering, baseline inactivity and isolated laboratory compilation |
| Common ownership tests | Passed pending-intent round trip and rejection of unknown or duplicate action records |
| Native macOS ARM64 rotation | Passed actual CLI rotation, credential-generation change, overlap expiry, duplicate refusal and common process cleanup |
| OrbStack V04 | Passed operator overlap/expiry, per-node runtime rotation and duplicate refusal; [results](../../outputs/p9-v04-orbstack-acceptance/results.json), [actions](../../outputs/p9-v04-orbstack-acceptance/actions.log), [before](../../outputs/p9-v04-orbstack-acceptance/before.json), [after](../../outputs/p9-v04-orbstack-acceptance/after.json) |
| Quick | Passed all 40 development tests; [log](../../outputs/verification/20260914T004444Z-quick.log) |
| Portable | Passed CTest, normalized consumers, HTTP integration, web build and native lifecycle/rotation; [log](../../outputs/verification/20260914T004721Z-portable.log) |
| Quality | Passed formatting, clang-tidy and cppcheck, including the Linux process observation correction; [log](../../outputs/verification/20260914T004457Z-quality.log) |
| Linux process exit regression | Passed 100 consecutive runs (2,500 exit fixtures); [log](../../outputs/p9-lima-preparation/linux-exit-race.log) |
| Linux ARM64 native and guest packages | Passed all 42 native tests, archive verification and combined native/platform/guest installation; [build](../../outputs/p9-lima-preparation/native-release.log), [installation](../../outputs/p9-lima-preparation/install.log) |
| Installed scenario preflight | Passed S11/S12/S14 laboratory/S15 compilation and scenario plans without creating runtime state; [result](../../outputs/p9-lima-preparation/preflight.log) |
| Design package | Passed 24 positive cases, 15 negative inputs and 63 target sets; [static checker](../../outputs/p9-lima-preparation/design-check.json) |
| ShellCheck | Passed the exact command below |
| Lima S11 | Passed baseline inactivity, netem apply/automatic expiry/clear and cleanup; [results](../../outputs/p9-lima-acceptance/S11/results.json) |
| Lima S12 | Passed absent/apply/clear packet checks, pending-intent replay refusal and cleanup; [results](../../outputs/p9-lima-acceptance/S12/results.json) |
| Lima S14 laboratory | Passed isolated simulator samples/results, authenticated control, test-only trust and cleanup; [results](../../outputs/p9-lima-acceptance/S14/results.json) |
| Lima S15, x86_64 TCG | Passed actual boot, bidirectional TCP/UDP, declared scenario traffic/VLAN/capture/QMP and cleanup; [results](../../outputs/p9-lima-acceptance/S15/results.json), [scenario](../../outputs/p9-lima-acceptance/S15/scenario.log) |
| Corrected laboratory package | Passed 42 tests and verified installation at `/var/lib/graphx/runtime/p9/installed-lab`; [build](../../outputs/p9-lima-acceptance/native-lab-release.log) |

```sh
shellcheck -x -P .:scripts/lib:infrastructure/lima scripts/lib/demo-runtime.sh examples/static-route-policy/scripts/demo.sh
```

The scenario harness is [test_scenario_live.py](../../tests/test_scenario_live.py).
V04 runs without privilege on OrbStack. Networking cases require explicit
`--allow-privileged` and Linux root. The S15 extension is in
[test_guest_execution_live.py](../../tests/test_guest_execution_live.py).
Both preserve before/after infrastructure inventories and use common cleanup.

The Linux package check exposed a transient `/proc/PID/exe` disappearance before
the zombie state becomes visible. The common process observer now reports that
intermediate observation as unavailable, preserving fail-closed ownership checks.
The existing exit-race regression passed 100 consecutive runs after the correction.

S14 initially failed because the external attachment had no owned switch when
selected as a simulator. Cleanup succeeded. Selection now derives the network's
single owned switch and rejects missing or ambiguous bindings; focused positive
and ambiguity tests pass. S14 passed using the rebuilt verified package.
Before/after inventories and run/cleanup logs are retained under
`outputs/p9-lima-acceptance/S11`, `S12`, `S14` and `S15`; captures and private
runtime material remain in guest storage. All four cleanup return codes are zero.

## Privileged acceptance commands

These cases ran with authorization in the existing GraphX Lima guest. No VM
provisioning, Docker context changes or privileged socket forwarding was needed.
The verified combined development installation is `/var/lib/graphx/runtime/p9/installed`,
based on commit `f8aea052fe617884b1d2c5c901a0f7a6e0647d26` plus the P9 worktree
changes. It is an explicitly dirty candidate, not a published release.
Each evidence directory must be absent. The S14 case selects the isolated simulator;
it does not attach or test a physical device.

```sh
for CASE in S11 S12; do
  limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
    python3 tests/test_scenario_live.py --allow-privileged --target lima \
    --case "$CASE" --release /var/lib/graphx/runtime/p9/installed \
    --images /var/lib/graphx/runtime/p7-acceptance/images-store \
    --output "/var/lib/graphx/runtime/p9/acceptance-$CASE"
done
limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  python3 tests/test_scenario_live.py --allow-privileged --target lima \
  --case S14 --release /var/lib/graphx/runtime/p9/installed-lab \
  --images /var/lib/graphx/runtime/p7-acceptance/images-store \
  --output /var/lib/graphx/runtime/p9/acceptance-S14-bound
limactl shell --workdir /workspace/graphx-docker graphx -- sudo -n \
  python3 tests/test_guest_execution_live.py --allow-privileged --target lima \
  --case S15 --scenario --release /var/lib/graphx/runtime/p9/installed \
  --images /var/lib/graphx/runtime/p7-acceptance/images-store \
  --guests /var/lib/graphx/runtime/p9/guests \
  --output /var/lib/graphx/runtime/p9/acceptance-S15
```

Native Linux x86_64, physical-device,
KVM and browser evidence are not established by the portable tests or P8 boot
results. Independent clean repeat-build reproducibility is not established for
these cache-assisted, explicitly dirty development candidates.
