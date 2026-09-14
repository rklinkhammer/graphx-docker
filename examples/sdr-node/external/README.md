# External SDR and explicit laboratory selection

The physical declaration supports validation and compilation. Physical startup stays
gated until an explicit uplink ownership contract exists. An external address does
not authorize connecting a host NIC or starting a substitute.

For the isolated laboratory, compile this same graph with
`--laboratory laboratory-radio`. Inspect `laboratory-selection.json`, then start
that compilation through the common `graphx run` lifecycle in the explicitly
authorized Linux/Lima environment. The selected radio uses the shared SDR image,
an owned container veth and the declared test credentials. No physical attachment
or production trust is reused.

The original graph and external credential files remain unchanged. `run down`
removes the laboratory's staged credentials and owned resources while retaining
ordinary history/capture evidence. The common compiled laboratory interface passed privileged Lima acceptance.
The wrapper delegates to the common compiled runner.

See [scenario execution](../../../docs/scenarios.md), [P9 verification](../../../design/graph-generation/p9-verification.md)
and the [example matrix](../../README.md).
