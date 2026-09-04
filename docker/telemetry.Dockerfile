FROM node:22-alpine@sha256:c610fcdfb1d5b4740dd70c284ed3cb16bb857e0f7166196e36a5501df7a3aa32 AS web
WORKDIR /app/web
COPY web/package*.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

FROM node:22-alpine@sha256:c610fcdfb1d5b4740dd70c284ed3cb16bb857e0f7166196e36a5501df7a3aa32
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
    GRAPHX_VERSION=${GRAPHX_VERSION}
USER node
EXPOSE 8080
CMD ["node", "server.mjs"]
