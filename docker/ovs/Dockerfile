FROM ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      iproute2 iputils-ping nftables openvswitch-switch tcpdump \
 && rm -rf /var/lib/apt/lists/*
COPY docker/ovs/entrypoint.sh /usr/local/bin/graphx-ovs
ENTRYPOINT ["/usr/local/bin/graphx-ovs"]
