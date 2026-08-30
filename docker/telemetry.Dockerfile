FROM node:22-alpine AS web
WORKDIR /app/web
COPY web/package*.json ./
RUN npm install
COPY web/ ./
RUN npm run build

FROM node:22-alpine
WORKDIR /app
RUN mkdir /captures && chmod 0777 /captures
COPY apps/telemetry/package*.json ./
RUN npm ci --omit=dev
COPY apps/telemetry/server.mjs ./server.mjs
COPY graphx.yaml ./graphx.yaml
COPY --from=web /app/web/dist ./web/dist
ENV GRAPHX_WEB_ROOT=/app/web/dist GRAPHX_CONFIG=/app/graphx.yaml PORT=8080
USER node
EXPOSE 8080
CMD ["node", "server.mjs"]
