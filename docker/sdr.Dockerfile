FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS trust
COPY docker/debian.sources /etc/apt/sources.list.d/debian.sources
RUN apt-get update && apt-get install -y --no-install-recommends bash ca-certificates curl \
 && rm -rf /var/lib/apt/lists/* /var/log/* /var/cache/ldconfig/aux-cache
COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust

FROM python:3.13-alpine@sha256:7415fbc3c9e4979cc717d92377ab2bc7b2b4a2af1ac03cc52b5f3f88efedaf3a
COPY --from=trust /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
ARG GRAPHX_VERSION=dev
ARG GRAPHX_REVISION=unknown
LABEL org.opencontainers.image.title="GraphX SDR services" \
      org.opencontainers.image.source="https://github.com/rklinkhammer/graphx-docker" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${GRAPHX_VERSION}" \
      org.opencontainers.image.revision="${GRAPHX_REVISION}"
ENV PYTHONDONTWRITEBYTECODE=1 GRAPHX_VERSION=${GRAPHX_VERSION} SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
RUN addgroup -S -g 65532 graphx && adduser -S -D -H -u 65532 -G graphx graphx
WORKDIR /opt/graphx-sdr
COPY examples/sdr-node/common/*.py ./common/
COPY LICENSE THIRD_PARTY.md /usr/local/share/doc/graphx/
USER 65532:65532
COPY docker/sdr-entrypoint.py /usr/local/bin/graphx-sdr
USER root
RUN chmod 0555 /usr/local/bin/graphx-sdr \
 && ln -s graphx-sdr /usr/local/bin/graphx-sdr-radio \
 && ln -s graphx-sdr /usr/local/bin/graphx-sdr-processor \
 && ln -s graphx-sdr /usr/local/bin/graphx-sdr-sink
USER 65532:65532
CMD ["/usr/local/bin/graphx-sdr", "--help"]
