# Explicit scenario actions

Compile the authored `scenario.actions` with the graph. Baseline `graphx run up`
never executes them. Inspect or select one action by its declared ID:

```sh
"$GX_RELEASE/bin/graphx" scenario plan --output "$GX_OUTPUT" --state-root "$GX_STATE"
"$GX_RELEASE/bin/graphx" scenario run --action ACTION_ID --output "$GX_OUTPUT" \
  --state-root "$GX_STATE" --release "$GX_RELEASE"
"$GX_RELEASE/bin/graphx" scenario status --action ACTION_ID --output "$GX_OUTPUT" \
  --state-root "$GX_STATE"
```

Networking and QMP actions require an authorized local Linux root invocation with
`--allow-privileged`. On macOS use the existing GraphX Lima guest; these commands do
not provision or select a VM. Credential rotation also supports native applications
and unprivileged Compose on OrbStack. A scenario requires the exact compilation and
a ready graph. Unknown IDs, fields, references and operations fail before mutation.
The CLI verifies the compilation manifest and resolved references, takes the existing
exclusive graph lock, and writes bounded action records into `ownership.yml`.

| Action | Behavior |
|---|---|
| `fault` | Applies netem to an identity-owned data attachment. The existing fault timer clears it at its bounded monotonic deadline; `scenario clear --action ID` explicitly clears an owned fault sooner. It refuses an existing custom qdisc. |
| `route-apply` | Adds an absent, declared manual route in its owned router namespace. It records the observed route identity and never replaces an existing route. |
| `route-clear` | Deletes only the matching route recorded by its declared apply action. `scenario clear` on the apply action has the same ownership check. |
| `traffic` | Runs bounded checks on declared connections. Diagnostic UDP probes carry unique tokens and check receipt or absence. Guest checks cover its resolved unicast TCP/UDP contract, VLAN isolation, nonempty capture and identity-checked QMP pause/resume. |
| `credential-rotate` | Publishes the declared next reference through the common credential-generation implementation, preserving provider, identity and member roles. The overlap is 1–60 seconds and expires across reader restart. No credential values enter plans, logs or command arguments. |
| `external-simulator` | Requires explicit pre-start `compile --laboratory ACTION_ID`; it cannot replace a running physical device. |

`scenario status` reports the recorded action outcome (`not-run`, `pending`,
`complete`, or `cleared`); use `run status` for graph health. A timed fault's action
record remains complete after its timer expires. Clear it before applying it again.
Completed rotations cannot be replayed. Traffic checks may be repeated after success.
Routes can be applied again after their owned clear.

A pending record refuses retry: it may describe an interrupted mutation or failed
verification. Keep that record for inspection, then use the common identity-checked
`run down` and a fresh `run up` to recover the graph before selecting the action
again. A process, route, qdisc, directory or resource identity mismatch fails closed.
Cleanup never recreates a baseline or adopts a same-named external resource. History
and bounded capture evidence remain under the graph's ordinary retention policy.

## Laboratory selection and physical trust

```sh
"$GX_RELEASE/bin/graphx" compile examples/sdr-node/external/graphx.yml \
  --laboratory laboratory-radio --target lima --catalog-root "$GRAPHX_CATALOG" \
  --source-root "$GRAPHX_SOURCE" --credential-root "$GX_CREDENTIALS" \
  --output "$GX_OUTPUT"
```

This explicit selection resolves the declared radio as `sdr.simulator`, converts its
attachment into an owned container veth, and substitutes the declared laboratory
credentials for the radio and processor. The compilation records the selection in
`laboratory-selection.json`. When the external attachment has no switch, selection requires exactly one owned
switch among attachments on that network; missing or ambiguous bindings fail.
It uses the ordinary shared SDR image and OVS lifecycle;
there is no separate simulator launcher, source manifest or physical uplink.

The selected graph has an isolated owned bridge with no physical-device attachment.
It neither probes nor takes ownership of a real radio. Physical startup stays gated
until the separate uplink ownership contract exists. The original graph and external
credential files remain unchanged; stopping the laboratory removes its staged test
credentials. Returning to physical operation requires a separate compilation of the
original declaration and the future physical-uplink gate. It never silently reuses
laboratory trust for a physical device.

See [P9 verification](../design/graph-generation/p9-verification.md) for actual test
coverage and the remaining privileged acceptance commands.
