# syntax=docker/dockerfile:1.7

FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      bash ca-certificates cmake curl ninja-build g++ libssl-dev \
 && rm -rf /var/lib/apt/lists/*
COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust
ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
WORKDIR /src
COPY CMakeLists.txt ./
COPY VERSION LICENSE THIRD_PARTY.md README.md CHANGELOG.md SECURITY.md SUPPORT.md CONTRIBUTING.md ./
COPY cmake cmake
COPY include include
COPY src src
COPY apps apps
COPY config config
COPY docs docs
COPY tools tools
COPY wireshark wireshark
COPY graphx.yaml ./graphx.yaml
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRAPHX_BUILD_TESTS=OFF \
 && cmake --build build

FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171
ARG GRAPHX_VERSION=dev
ARG GRAPHX_REVISION=unknown
LABEL org.opencontainers.image.title="GraphX runtime" \
      org.opencontainers.image.description="Transport-neutral GraphX runtime and CLI" \
      org.opencontainers.image.source="https://github.com/rklinkhammer/graphx-docker" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${GRAPHX_VERSION}" \
      org.opencontainers.image.revision="${GRAPHX_REVISION}"
RUN apt-get update && apt-get install -y --no-install-recommends iproute2 libssl3 && rm -rf /var/lib/apt/lists/*
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
RUN mkdir /captures && chown 65532:65532 /captures && chmod 0770 /captures
COPY --from=build /src/build/graphx-generator /usr/local/bin/
COPY --from=build /src/build/graphx-transform /usr/local/bin/
COPY --from=build /src/build/graphx-sink /usr/local/bin/
COPY --from=build /src/build/graphx-udp-publisher /usr/local/bin/
COPY --from=build /src/build/graphx-udp-subscriber /usr/local/bin/
COPY --from=build /src/build/graphx /usr/local/bin/
COPY --from=build /src/graphx.yaml /etc/graphx/graphx.yaml
ENV GRAPHX_CONFIG=/etc/graphx/graphx.yaml GRAPHX_VERSION=${GRAPHX_VERSION} \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
USER 65532:65532
ENTRYPOINT ["/usr/local/bin/graphx-generator"]
