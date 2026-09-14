# Credential rotation

Compile `graphx.yml` with verified image pins and stage explicit private external
`operator/token` and `operator-next/token` files through `run up --external DIR`.
Baseline startup performs no rotation. With the graph ready:

```sh
"$GX_RELEASE/bin/graphx" scenario run --action rollover --output "$GX_OUTPUT" --state-root "$GX_STATE"
"$GX_RELEASE/bin/graphx" scenario run --action runtime-rollover --output "$GX_OUTPUT" --state-root "$GX_STATE"
```

The first action rotates the operator token; the second rotates only the generator's
runtime HMAC. Both preserve identity and allow the declared 60-second overlap.
Expired previous credentials are rejected. Plans and action logs contain references,
never secret values. See [scenario execution](../../../docs/scenarios.md).
