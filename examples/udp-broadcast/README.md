# UDP broadcast example

This example confines directed broadcast traffic to an internal Docker network
at `172.31.91.0/24`; it never chooses or transmits through a physical host
interface. Docker with Compose and a locally available GraphX runtime image are
required. Prepare the image while dependencies are available, or load the same
image from an offline artifact:

```sh
docker build -t graphx-demo:latest .
# Alternatively: docker load -i graphx-demo.tar
```

The example run itself never builds or pulls an image. To use a differently
tagged preloaded image, export `GRAPHX_BROADCAST_IMAGE` before both validation
and execution.

```sh
examples/udp-broadcast/run.sh
```

The run succeeds when the listener receives five discovery announcements. The
cleanup trap removes the isolated network after success, failure, or interruption.
Routers normally do not forward broadcast traffic, and host/container firewall
rules can still reject it.

## Native Linux acceptance

The privileged acceptance runner creates two network namespaces and connects
them only to a temporary Linux bridge. The bridge has no physical interface or
default route, so the directed broadcast cannot leave the disposable lab:

```sh
GRAPHX_BUILD_DIR="$PWD/build/dev" examples/udp-broadcast/run-native-linux.sh
examples/udp-broadcast/down-native-linux.sh
examples/udp-broadcast/down-native-linux.sh
```

Both scripts require `sudo` and `iproute2`. The runner refuses pre-existing
resources with its reserved names, cleans up after normal completion or a
signal, and expects `PASS received=5`. The cleanup command is idempotent and is
shown twice deliberately. Docker Desktop does not substitute for this native
Linux acceptance test.

When `dumpcap` and `tshark` are installed, the same isolated run can also prove
that live datagrams decode as GraphX sequences 1 through 5:

```sh
GRAPHX_BUILD_DIR="$PWD/build/dev" GRAPHX_VERIFY_LIVE_CAPTURE=1 \
  examples/udp-broadcast/run-native-linux.sh
```

Capture is limited to the disposable publisher veth and UDP destination port
47102. No physical interface is opened. `scripts/test-features.sh
linux-network` enables this check automatically when both tools are available
and reports an explicit skip otherwise.
