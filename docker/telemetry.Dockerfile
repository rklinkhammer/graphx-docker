# syntax=docker/dockerfile:1.7

FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171 AS trust
RUN apt-get update && apt-get install -y --no-install-recommends bash ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*
COPY docker/install-build-trust.sh /usr/local/libexec/graphx-install-build-trust
ARG GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
RUN --mount=type=secret,id=graphx_ca,required=false \
    --mount=type=secret,id=graphx_cert_installer,required=false \
    /usr/bin/bash /usr/local/libexec/graphx-install-build-trust

FROM node:26-alpine@sha256:2d984a15c9b54fd0aeb608b8e0d0d83529eb34d2966db27a1fb4f1edc3d298a3 AS web
COPY --from=trust /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
ENV NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt \
    npm_config_cafile=/etc/ssl/certs/ca-certificates.crt
WORKDIR /app/web
COPY web/package*.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

FROM node:26-alpine@sha256:2d984a15c9b54fd0aeb608b8e0d0d83529eb34d2966db27a1fb4f1edc3d298a3
COPY --from=trust /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
ARG GRAPHX_VERSION=dev
ARG GRAPHX_REVISION=unknown
LABEL org.opencontainers.image.title="GraphX telemetry" \
      org.opencontainers.image.description="GraphX telemetry, control, history, and web console service" \
      org.opencontainers.image.source="https://github.com/rklinkhammer/graphx-docker" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${GRAPHX_VERSION}" \
      org.opencontainers.image.revision="${GRAPHX_REVISION}"
WORKDIR /app
RUN addgroup -S -g 65532 graphx-capture && addgroup node graphx-capture \
 && mkdir /captures /var/lib/graphx /var/lib/graphx/history \
 && chown 65532:65532 /captures && chmod 0770 /captures \
 && chown -R node:node /var/lib/graphx && chmod 0750 /var/lib/graphx /var/lib/graphx/history
COPY apps/telemetry/package*.json ./
RUN npm ci --omit=dev
COPY --chown=node:node apps/telemetry/server.mjs apps/telemetry/security.mjs apps/telemetry/control.mjs apps/telemetry/operations.mjs \
  apps/telemetry/history.mjs apps/telemetry/history-worker.mjs apps/telemetry/capture-files.mjs ./
COPY graphx.yaml ./graphx.yaml
COPY --from=web /app/web/dist ./web/dist
ENV GRAPHX_WEB_ROOT=/app/web/dist GRAPHX_CONFIG=/app/graphx.yaml PORT=8080 \
    GRAPHX_VERSION=${GRAPHX_VERSION} \
    NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt \
    SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt \
    npm_config_cafile=/etc/ssl/certs/ca-certificates.crt
USER node
EXPOSE 8080
CMD ["node", "server.mjs"]
