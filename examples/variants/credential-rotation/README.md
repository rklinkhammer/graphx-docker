# Credential rotation

Provide private `operator/token` and `operator-next/token` files in an external
credential directory. From the repository root:

```sh
graphx example up variants/credential-rotation --external /absolute/credentials
graphx example scenario variants/credential-rotation --action rollover
graphx example tokens variants/credential-rotation
graphx example scenario variants/credential-rotation --action runtime-rollover
graphx example down variants/credential-rotation
```

Baseline startup performs no rotation. The first action rotates the operator token;
the second rotates the generator runtime HMAC. Both use the declared 60-second
overlap. See [scenario execution](../../../docs/user-guide.md#scenario-actions) and the
[CLI quick start](../../quick-start.md).
