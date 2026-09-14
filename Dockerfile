FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS build
COPY docker/debian.sources /etc/apt/sources.list.d/debian.sources
RUN apt-get update && apt-get install -y --no-install-recommends \
      bash ca-certificates cmake curl ninja-build g++ libssl-dev \
 && rm -rf /var/lib/apt/lists/* /var/log/* /var/cache/ldconfig/aux-cache
COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust
ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
WORKDIR /src
COPY CMakeLists.txt ./
COPY VERSION LICENSE THIRD_PARTY.md README.md SECURITY.md SUPPORT.md CONTRIBUTING.md ./
COPY cmake cmake
COPY include include
COPY src src
COPY apps apps
COPY config config
COPY docs docs
COPY deploy deploy
COPY tools tools
COPY wireshark wireshark
ARG GRAPHX_RELEASE_IMAGE_TYPES=OFF
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRAPHX_BUILD_TESTS=OFF \
      -DGRAPHX_RELEASE_IMAGE_TYPES=${GRAPHX_RELEASE_IMAGE_TYPES} \
 && cmake --build build

FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS runtime
COPY docker/debian.sources /etc/apt/sources.list.d/debian.sources
ARG GRAPHX_VERSION=dev
ARG GRAPHX_REVISION=unknown
LABEL org.opencontainers.image.title="GraphX runtime" \
      org.opencontainers.image.description="Transport-neutral GraphX runtime and CLI" \
      org.opencontainers.image.source="https://github.com/rklinkhammer/graphx-docker" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${GRAPHX_VERSION}" \
      org.opencontainers.image.revision="${GRAPHX_REVISION}"
RUN apt-get update && apt-get install -y --no-install-recommends iproute2 libssl3 openssl && rm -rf /var/lib/apt/lists/* /var/log/* /var/cache/ldconfig/aux-cache
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
RUN mkdir /captures && chown 65532:65532 /captures && chmod 0770 /captures
COPY --from=build /src/build/graphx-generator /usr/local/bin/
COPY --from=build /src/build/graphx-transform /usr/local/bin/
COPY --from=build /src/build/graphx-sink /usr/local/bin/
COPY --from=build /src/build/graphx-udp-publisher /usr/local/bin/
COPY --from=build /src/build/graphx-udp-subscriber /usr/local/bin/
COPY --from=build /src/build/graphx /usr/local/bin/
COPY --from=build /src/build/generated/graphx-build-dependencies.json /usr/local/share/graphx/build-dependencies.json
COPY --from=build /src/build/_deps/yaml-cpp-src/LICENSE /usr/local/share/doc/graphx/yaml-cpp-LICENSE
COPY LICENSE THIRD_PARTY.md /usr/local/share/doc/graphx/
ENV GRAPHX_VERSION=${GRAPHX_VERSION} \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
USER 65532:65532
CMD ["/usr/local/bin/graphx", "--help"]

FROM runtime AS sdr
LABEL org.opencontainers.image.title="GraphX SDR services"
USER root
RUN apt-get update && apt-get install -y --no-install-recommends python3 \
 && rm -rf /var/lib/apt/lists/* /var/log/* /var/cache/ldconfig/aux-cache
WORKDIR /opt/graphx-sdr
COPY examples/sdr-node/common/*.py ./common/
COPY docker/sdr-entrypoint.py /usr/local/bin/graphx-sdr
RUN chmod 0555 /usr/local/bin/graphx-sdr \
 && ln -s graphx-sdr /usr/local/bin/graphx-sdr-radio \
 && ln -s graphx-sdr /usr/local/bin/graphx-sdr-processor \
 && ln -s graphx-sdr /usr/local/bin/graphx-sdr-sink
ENV PYTHONDONTWRITEBYTECODE=1
USER 65532:65532
CMD ["/usr/local/bin/graphx-sdr", "--help"]

FROM node:24.20.0-bookworm-slim@sha256:ba849c60be29959425b8734d57b8b4b7d56f98edd9504c9af091d5281095a71e AS web
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt npm_config_cafile=/etc/ssl/certs/ca-certificates.crt
WORKDIR /app/web
COPY web/package*.json ./
RUN npm ci && rm -rf /root/.npm /tmp/node-compile-cache
COPY web/ ./
RUN npm run build
WORKDIR /app
COPY apps/telemetry/package*.json ./
RUN npm ci --omit=dev && rm -rf /root/.npm /tmp/node-compile-cache

FROM runtime AS telemetry
LABEL org.opencontainers.image.title="GraphX platform"
USER root
COPY --from=web /usr/local/bin/node /usr/local/bin/node
COPY --from=web /app/node_modules /app/node_modules
COPY --from=web /app/web/dist /app/web/dist
COPY apps/telemetry/node-console.mjs apps/telemetry/console-session.mjs apps/telemetry/output-bound.mjs apps/telemetry/server.mjs apps/telemetry/security.mjs apps/telemetry/control.mjs \
  apps/telemetry/operations.mjs apps/telemetry/history.mjs apps/telemetry/history-worker.mjs \
  apps/telemetry/capture-files.mjs apps/telemetry/normalized-config.mjs apps/telemetry/topology.mjs \
  apps/telemetry/metric-store.mjs apps/telemetry/runtime-evidence.mjs apps/telemetry/http-routes.mjs \
  apps/telemetry/collector.mjs apps/telemetry/credentials.mjs apps/telemetry/platform-config.mjs \
  apps/telemetry/platform.mjs /app/
RUN mkdir -p /var/lib/graphx/history \
 && chown -R 65532:65532 /var/lib/graphx \
 && chmod 0700 /var/lib/graphx /var/lib/graphx/history \
 && printf '#!/bin/sh\nexec node /app/platform.mjs "$@"\n' > /usr/local/bin/graphx-platform \
 && chmod 0555 /usr/local/bin/graphx-platform
COPY config/schema/normalized-graph.schema.json /config/schema/normalized-graph.schema.json
COPY web/package-lock.json /usr/local/share/graphx/web-package-lock.json
ENV GRAPHX_WEB_ROOT=/app/web/dist NODE_VERSION=24.20.0
WORKDIR /app
USER 65532:65532
EXPOSE 8080
ENTRYPOINT []
CMD ["graphx-platform", "--help"]

FROM runtime AS default
