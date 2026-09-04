# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash ca-certificates curl iproute2 iputils-ping nftables openvswitch-switch tcpdump tshark \
 && rm -rf /var/lib/apt/lists/*
COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
COPY docker/ovs/entrypoint.sh /usr/local/bin/graphx-ovs
ENTRYPOINT ["/usr/local/bin/graphx-ovs"]
