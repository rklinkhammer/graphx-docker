# Standalone macvlan pipeline

This Linux-only example places all three independently deployed GraphX nodes on
one macvlan L2 domain. Every container has an explicit IP and MAC address. The
helper creates an isolated dummy parent, so the demo does not touch the host's
physical LAN.

```sh
cmake --preset dev && cmake --build --preset dev
./build/dev/graphx validate examples/macvlan/graphx.yaml
examples/macvlan/scripts/up.sh
docker logs -f gx-mac-sink-sink-1
examples/macvlan/scripts/status.sh
examples/macvlan/scripts/down.sh
```

The generator, transform, and sink use three separate Compose projects. The
external Docker network is owned by `graphx infra`, not by Compose. As with all
macvlan networks, the parent host cannot directly contact macvlan children
without a separate host-side macvlan shim.
