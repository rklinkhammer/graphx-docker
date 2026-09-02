FROM node:22-alpine AS web
WORKDIR /app/web
COPY web/package*.json ./
RUN npm ci
COPY web/ ./
RUN npm run build

FROM node:22-alpine
WORKDIR /app
RUN addgroup -S -g 65532 graphx-capture && addgroup node graphx-capture \
 && mkdir /captures && chown 65532:65532 /captures && chmod 0770 /captures
COPY apps/telemetry/package*.json ./
RUN npm ci --omit=dev
COPY --chown=node:node apps/telemetry/server.mjs apps/telemetry/security.mjs apps/telemetry/operations.mjs ./
COPY graphx.yaml ./graphx.yaml
COPY --from=web /app/web/dist ./web/dist
ENV GRAPHX_WEB_ROOT=/app/web/dist GRAPHX_CONFIG=/app/graphx.yaml PORT=8080
USER node
EXPOSE 8080
CMD ["node", "server.mjs"]
