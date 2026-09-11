# External SDR OVS laboratory

The external SDR profile uses system OVS, container veth pairs, an explicit
external boundary, SPAN capture, and the shared GraphX ownership lifecycle.
MACVLAN is a semantic profile only; Compose supplies management connectivity and
does not create a Docker macvlan network.

```sh
examples/sdr-node/external/scripts/demo.sh plan
examples/sdr-node/external/scripts/demo.sh up
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh down
```

Run on native Linux or in Lima. Review and configure the external attachment
boundary before connecting real radio hardware. The version-1 model remains at
`examples/compatibility/v1/sdr-external.yaml` solely for migration.
