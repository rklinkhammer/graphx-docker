# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILDROOT_VERSION=2025.02.17
ARG BUILDROOT_SHA256=13618704563ad0b928a4564aaa73e2db97e12e8df0ed5ae874744a83964a023a
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bash bc bison build-essential ca-certificates cpio curl file flex git \
      libncurses-dev python3 rsync unzip wget xz-utils \
 && rm -rf /var/lib/apt/lists/*

COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust

RUN curl --fail --location --silent --show-error \
      "https://buildroot.org/downloads/buildroot-${BUILDROOT_VERSION}.tar.xz" \
      --output /tmp/buildroot.tar.xz \
 && echo "${BUILDROOT_SHA256}  /tmp/buildroot.tar.xz" | sha256sum --check --strict \
 && mkdir -p /opt/buildroot \
 && tar --extract --xz --file /tmp/buildroot.tar.xz \
      --directory /opt/buildroot --strip-components=1 \
 && unlink /tmp/buildroot.tar.xz

WORKDIR /workspace
ENTRYPOINT ["/bin/bash"]
