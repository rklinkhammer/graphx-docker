import dgram from 'node:dgram'
import { closeSync, createReadStream, existsSync, openSync, readFileSync, readSync, readdirSync, statSync } from 'node:fs'
import { createServer } from 'node:http'
import { createServer as createSecureServer } from 'node:https'
import { extname, join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocketServer } from 'ws'
import { parse } from 'yaml'
import { MAX_DATAGRAM_BYTES, RateLimiter, ReplayCache, isLoopback, originAllowed,
  parseRequestUrl, readSecret, signEnvelope, tokenMatches, validateTelemetryEvent,
  verifyEnvelope, webSocketBearer } from './security.mjs'
import { OtlpHttpExporter, SloEvaluator, graphReadiness, otlpConfig, otlpMetricsRequest,
  otlpTraceRequest } from './operations.mjs'

const root = normalize(process.env.GRAPHX_WEB_ROOT || join(fileURLToPath(new URL('.', import.meta.url)), '../../web/dist'))
const port = Number(process.env.PORT || 8080)
const udpPort = Number(process.env.GRAPHX_TELEMETRY_PORT || 9000)
const httpBind = process.env.GRAPHX_HTTP_BIND || '127.0.0.1'
const udpBind = process.env.GRAPHX_TELEMETRY_BIND || '127.0.0.1'
const configPath = process.env.GRAPHX_CONFIG || normalize(join(fileURLToPath(new URL('.', import.meta.url)), '../../graphx.yaml'))
const config = parse(readFileSync(configPath, 'utf8'))
const graph = config.graph || { id: 'graphx', nodes: [], edges: [] }
const deployment = config.deployment?.services || {}
const transport = config.transport || {}
const heartbeatTimeout = Number(process.env.GRAPHX_HEARTBEAT_TIMEOUT_MS || config.observability?.telemetry?.heartbeat_timeout_ms || 5000)
const websocketPath = config.observability?.telemetry?.websocket || '/ws'
const configuredCapture = config.observability?.capture || { enabled: false, provider: '' }
const sloEvaluator = new SloEvaluator(config.observability?.slos)
const configuredOtlp = otlpConfig(config.observability?.otlp)
const otlpExporter = new OtlpHttpExporter(configuredOtlp)
const captureConfig = { ...configuredCapture,
  enabled: process.env.GRAPHX_CAPTURE_ENABLED == null ? Boolean(configuredCapture.enabled) :
    ['1', 'true', 'yes', 'on'].includes(process.env.GRAPHX_CAPTURE_ENABLED.toLowerCase()),
  provider: process.env.GRAPHX_CAPTURE_PROVIDER || configuredCapture.provider || '',
}
const captureDirectory = resolve(process.env.GRAPHX_CAPTURE_DIR || captureConfig.directory || 'captures')
const controlToken = readSecret('GRAPHX_CONTROL_TOKEN')
const observationToken = readSecret('GRAPHX_OBSERVATION_TOKEN')
const telemetrySecret = readSecret('GRAPHX_TELEMETRY_SHARED_SECRET')
const tlsCertificateFile = process.env.GRAPHX_TLS_CERT_FILE || ''
const tlsPrivateKeyFile = process.env.GRAPHX_TLS_KEY_FILE || ''
const tlsClientCaFile = process.env.GRAPHX_TLS_CLIENT_CA_FILE || ''
if (Boolean(tlsCertificateFile) !== Boolean(tlsPrivateKeyFile))
  throw new Error('GRAPHX_TLS_CERT_FILE and GRAPHX_TLS_KEY_FILE must be provided together')
if (controlToken && !telemetrySecret)
  throw new Error('GRAPHX_TELEMETRY_SHARED_SECRET is required when runtime control is enabled')
if (!tlsCertificateFile && !isLoopback(httpBind) && process.env.GRAPHX_ALLOW_INSECURE_REMOTE !== 'true')
  throw new Error('plaintext telemetry may bind only to loopback; use TLS or explicitly set GRAPHX_ALLOW_INSECURE_REMOTE=true')
const allowedOrigins = new Set((process.env.GRAPHX_ALLOWED_ORIGINS || '').split(',').map(v => v.trim()).filter(Boolean))
const rateWindowSeconds = 5
const latencyBoundsUs = [10, 50, 100, 500, 1000, 5000, 10000]

function topologyModel() {
  const graphNodes = graph.nodes.map(node => ({
    id: node.id, label: node.id, role: node.kind, image: deployment[node.id]?.image || 'local process',
    input: node.ports.some(port => port.direction === 'input'),
    output: node.ports.some(port => port.direction === 'output'),
  }))
  const graphEdges = graph.edges.map(edge => {
    const [source] = edge.from.split('.')
    const [target, targetPort] = edge.to.split('.')
    const targetNode = graph.nodes.find(node => node.id === target)
    const schema = targetNode?.ports.find(port => port.name === targetPort)?.schema || 'unknown'
    const settings = transport[edge.transport]?.[edge.id] || {}
    return { id: edge.id, source, target, transport: edge.transport,
      port: settings.port || null, schema }
  })
  const network = config.network || {}
  const infrastructure = new Map(graphNodes.map(node => [node.id, node]))
  for (const item of network.networks || []) infrastructure.set(item.id, {
    id: item.id, label: item.id, role: `${item.driver}${item.mode ? ` ${item.mode}` : ''}`,
    image: (item.subnets || [item.subnet]).join(', '), input: true, output: true,
  })
  for (const item of network.switches || []) infrastructure.set(item.id, {
    id: item.id, label: item.id, role: 'Open vSwitch',
    image: item.mirror ? `SPAN · ${item.mirror.id}` : item.datapath || 'system',
    input: true, output: true,
  })
  for (const item of network.routers || []) infrastructure.set(item.id, {
    id: item.id, label: item.id, role: item.kind.replaceAll('_', ' '),
    image: item.forwarding === false ? 'forwarding off' : 'IPv4 forwarding',
    input: true, output: true,
  })
  return { graph: graph.id, nodes: graphNodes, edges: graphEdges,
    networkNodes: [...infrastructure.values()], edgePaths: network.edge_paths || {} }
}

const topology = topologyModel()
let state = { paused: false, fault: false, updatedAt: new Date().toISOString() }
const recent = []
const captureReferences = []
const controlEndpoints = new Map()
const controlAcks = new Map()
const nodes = Object.fromEntries(topology.nodes.map(node => [node.id, {
  status: 'starting', lastSeen: null, cpuPercent: null,
}]))
function emptyEdge(connection = 'disconnected') {
  return {
    sent: 0, received: 0, sentWireBytes: 0, receivedWireBytes: 0,
    drops: 0, errors: 0, reconnects: 0, backpressureEvents: 0,
    backpressureUs: 0, rejected: 0, connection, lastSequence: 0, lastSeen: null,
    latencyCount: 0, latencySumUs: 0,
    latencyBuckets: Array(latencyBoundsUs.length + 1).fill(0), rateBuckets: [],
  }
}

const edges = Object.fromEntries(topology.edges.map(edge => [edge.id, emptyEdge()]))
let serviceState = { httpReady: false, udpReady: false, shuttingDown: false }
let slo = sloEvaluator.snapshot()
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png' }

function recordRate(edge, timestamp, wireBytes) {
  const second = Math.floor(timestamp / 1000)
  edge.rateBuckets = edge.rateBuckets.filter(bucket => bucket.second > second - rateWindowSeconds)
  let bucket = edge.rateBuckets.find(value => value.second === second)
  if (!bucket) {
    bucket = { second, messages: 0, wireBytes: 0 }
    edge.rateBuckets.push(bucket)
  }
  bucket.messages += 1
  bucket.wireBytes += wireBytes
}

function currentRates(edge, timestamp = Date.now()) {
  const second = Math.floor(timestamp / 1000)
  const buckets = edge.rateBuckets.filter(bucket => bucket.second > second - rateWindowSeconds)
  if (!buckets.length) return { messageRate: 0, byteRate: 0 }
  const elapsed = Math.min(rateWindowSeconds,
    Math.max(1, second - Math.min(...buckets.map(bucket => bucket.second)) + 1))
  const messages = buckets.reduce((sum, bucket) => sum + bucket.messages, 0)
  const wireBytes = buckets.reduce((sum, bucket) => sum + bucket.wireBytes, 0)
  return {
    messageRate: Math.round(messages / elapsed * 10) / 10,
    byteRate: Math.round(wireBytes / elapsed),
  }
}

function latencyPercentile(edge, percentile) {
  if (!edge.latencyCount) return null
  const target = Math.ceil(edge.latencyCount * percentile)
  let cumulative = 0
  for (let index = 0; index < edge.latencyBuckets.length; ++index) {
    cumulative += edge.latencyBuckets[index]
    if (cumulative >= target)
      return index < latencyBoundsUs.length ? latencyBoundsUs[index] : latencyBoundsUs.at(-1)
  }
  return latencyBoundsUs.at(-1)
}

function edgeView(edge, timestamp = Date.now()) {
  const rates = currentRates(edge, timestamp)
  return {
    sent: edge.sent, received: edge.received,
    sentWireBytes: edge.sentWireBytes, receivedWireBytes: edge.receivedWireBytes,
    ...rates, drops: edge.drops, errors: edge.errors,
    meanLatencyUs: edge.latencyCount ? Math.round(edge.latencySumUs / edge.latencyCount) : null,
    p95LatencyUs: latencyPercentile(edge, 0.95), latencyCount: edge.latencyCount,
    reconnects: edge.reconnects, backpressureEvents: edge.backpressureEvents,
    backpressureUs: edge.backpressureUs, rejected: edge.rejected,
    connection: edge.connection, lastSequence: edge.lastSequence, lastSeen: edge.lastSeen,
    metricSources: {
      counters: 'measured', latency: edge.latencyCount ? 'measured' : 'unavailable',
      throughput: 'derived-5s', drops: 'measured',
    },
  }
}

function pcapngLinkType(path) {
  let descriptor
  try {
    descriptor = openSync(path, 'r')
    const bytes = Buffer.alloc(512)
    const count = readSync(descriptor, bytes, 0, bytes.length, 0)
    if (count < 24 || bytes.readUInt32LE(0) !== 0x0a0d0d0a) return null
    const sectionLength = bytes.readUInt32LE(4)
    if (sectionLength + 10 > count || bytes.readUInt32LE(sectionLength) !== 1) return null
    return bytes.readUInt16LE(sectionLength + 8)
  } catch { return null }
  finally { if (descriptor != null) closeSync(descriptor) }
}

function captureFiles() {
  if (!captureConfig.enabled || !existsSync(captureDirectory)) return []
  try {
    return readdirSync(captureDirectory, { withFileTypes: true })
      .filter(entry => entry.isFile() && /^[A-Za-z][A-Za-z0-9_-]{0,63}\.pcapng$/.test(entry.name))
      .map(entry => {
        const path = join(captureDirectory, entry.name)
        const details = statSync(path)
        const linkType = pcapngLinkType(path)
        return { name: entry.name, nodeId: entry.name.slice(0, -7), size: details.size,
          modifiedAt: details.mtime.toISOString(), url: `/captures/${encodeURIComponent(entry.name)}`,
          linkType, format: linkType === 1 ? 'ethernet' : linkType === 147 ? 'graphx-frame' : 'unknown' }
      })
  } catch { return [] }
}

function recentWithCapture() {
  return recent.slice(0, 30).map(event => ({ ...event,
    captures: captureReferences.filter(reference =>
      (event.messageId ? reference.messageId === event.messageId :
        reference.traceId === event.traceId && reference.sequence === event.sequence) &&
      reference.edgeId === event.edgeId).slice(0, 4),
  }))
}

function snapshot() {
  const timestamp = Date.now()
  const edgeViews = Object.fromEntries(
    Object.entries(edges).map(([id, edge]) => [id, edgeView(edge, timestamp)]))
  const liveEndpoints = [...controlEndpoints.values()].filter(endpoint =>
    timestamp - endpoint.lastSeen <= heartbeatTimeout)
  const files = captureFiles()
  const readiness = graphReadiness(nodes, edges, timestamp, heartbeatTimeout)
  return { kind: 'snapshot', graph: graph.id, topology,
    telemetry: { websocket: websocketPath, heartbeatTimeoutMs: heartbeatTimeout,
      rateWindowSeconds, latencyBoundsUs }, state,
    control: { available: controlToken.length > 0 && telemetrySecret.length > 0,
      authenticatedTelemetry: telemetrySecret.length > 0, connectedNodes: liveEndpoints.length,
      acknowledgements: Object.fromEntries(controlAcks) },
    health: { serviceReady: serviceState.httpReady && serviceState.udpReady && !serviceState.shuttingDown,
      graph: readiness }, slo, otlp: { enabled: configuredOtlp.enabled, ...otlpExporter.stats },
    nodes, edges: edgeViews,
    capture: { enabled: Boolean(captureConfig.enabled), provider: captureConfig.provider || null,
      format: files.length ? 'pcapng' : null,
      files }, recent: recentWithCapture(), timestamp }
}

const securityHeaders = {
  'content-security-policy': "default-src 'self'; connect-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'",
  'cross-origin-resource-policy': 'same-origin',
  'referrer-policy': 'no-referrer',
  'x-content-type-options': 'nosniff',
  'x-frame-options': 'DENY',
}

function json(response, status, value, extra = {}) {
  response.writeHead(status, { ...securityHeaders, 'cache-control': 'no-store',
    'content-type': 'application/json; charset=utf-8', ...extra })
  response.end(JSON.stringify(value))
}

function authorized(request, token) {
  return tokenMatches(token, request.headers.authorization || '')
}

function requestOriginAllowed(request) {
  const origin = request.headers.origin
  if (!origin) return true
  if (allowedOrigins.size) return originAllowed(origin, allowedOrigins)
  const scheme = tlsCertificateFile ? 'https' : 'http'
  return origin === `${scheme}://${request.headers.host}`
}

const rateLimits = {
  general: new RateLimiter(2048),
  control: new RateLimiter(2048),
}
function withinRateLimit(request, maximum, windowMs = 60000, scope = 'general') {
  const limiter = rateLimits[scope] || rateLimits.general
  return limiter.allow(request.socket.remoteAddress || 'unknown', maximum, windowMs)
}

function liveControlEndpoints(timestamp = Date.now()) {
  return [...controlEndpoints.entries()].filter(([, endpoint]) =>
    timestamp - endpoint.lastSeen <= heartbeatTimeout)
}

function prometheus() {
  const lines = [
    '# HELP graphx_edge_messages_total Messages observed on a GraphX edge.',
    '# TYPE graphx_edge_messages_total counter',
    '# HELP graphx_edge_wire_bytes_total Framed bytes observed on a GraphX edge.',
    '# TYPE graphx_edge_wire_bytes_total counter',
    '# HELP graphx_edge_message_rate Current sent-message throughput derived over five seconds.',
    '# TYPE graphx_edge_message_rate gauge',
    '# HELP graphx_edge_wire_byte_rate Current sent-byte throughput derived over five seconds.',
    '# TYPE graphx_edge_wire_byte_rate gauge',
    '# HELP graphx_edge_latency_seconds Edge receive latency histogram.',
    '# TYPE graphx_edge_latency_seconds histogram',
    '# HELP graphx_edge_errors_total Transport and decode errors observed on an edge.',
    '# TYPE graphx_edge_errors_total counter',
    '# HELP graphx_edge_dropped_total Frames explicitly dropped or rejected by policy.',
    '# TYPE graphx_edge_dropped_total counter',
    '# HELP graphx_edge_rejected_total Frames rejected by backpressure policy.',
    '# TYPE graphx_edge_rejected_total counter',
    '# HELP graphx_edge_reconnects_total Transport reconnect attempts observed on an edge.',
    '# TYPE graphx_edge_reconnects_total counter',
    '# HELP graphx_edge_backpressure_events_total Send operations that experienced backpressure.',
    '# TYPE graphx_edge_backpressure_events_total counter',
    '# HELP graphx_edge_backpressure_seconds_total Time spent waiting on transport backpressure.',
    '# TYPE graphx_edge_backpressure_seconds_total counter',
    '# HELP graphx_edge_connected Whether recent events report a connected transport path.',
    '# TYPE graphx_edge_connected gauge',
    '# HELP graphx_service_ready Whether the telemetry HTTP and UDP listeners are ready.',
    '# TYPE graphx_service_ready gauge',
    '# HELP graphx_graph_ready Whether all configured GraphX nodes and edges are ready.',
    '# TYPE graphx_graph_ready gauge',
    '# HELP graphx_slo_met Whether all GraphX SLO objectives are met after warm-up.',
    '# TYPE graphx_slo_met gauge',
    '# HELP graphx_slo_status Current SLO evaluator state as a one-hot gauge.',
    '# TYPE graphx_slo_status gauge',
    '# HELP graphx_slo_ratio Current value and configured target for dimensionless GraphX SLO objectives.',
    '# TYPE graphx_slo_ratio gauge',
    '# HELP graphx_slo_latency_seconds Current value and configured target for the GraphX latency SLO.',
    '# TYPE graphx_slo_latency_seconds gauge',
    '# HELP graphx_otlp_exports_total OTLP export outcomes.',
    '# TYPE graphx_otlp_exports_total counter',
    '# HELP graphx_otlp_queue_depth Current bounded OTLP export queue depth.',
    '# TYPE graphx_otlp_queue_depth gauge',
    '# HELP graphx_otlp_queue_bytes Current bounded OTLP export queue bytes.',
    '# TYPE graphx_otlp_queue_bytes gauge',
  ]
  const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
  lines.push(`graphx_service_ready ${serviceState.httpReady && serviceState.udpReady && !serviceState.shuttingDown ? 1 : 0}`)
  lines.push(`graphx_graph_ready ${readiness.ready ? 1 : 0}`)
  lines.push(`graphx_slo_met ${slo.met ? 1 : 0}`)
  for (const status of ['warming', 'met', 'violated'])
    lines.push(`graphx_slo_status{status="${status}"} ${slo.status === status ? 1 : 0}`)
  for (const [name, objective] of Object.entries(slo.objectives || {})) {
    if (name === 'p95LatencyUs') {
      if (objective.value != null) lines.push(`graphx_slo_latency_seconds{quantile="0.95",kind="value"} ${objective.value / 1e6}`)
      lines.push(`graphx_slo_latency_seconds{quantile="0.95",kind="target"} ${objective.target / 1e6}`)
    } else {
      if (objective.value != null) lines.push(`graphx_slo_ratio{objective="${name}",kind="value"} ${objective.value}`)
      lines.push(`graphx_slo_ratio{objective="${name}",kind="target"} ${objective.target}`)
    }
  }
  lines.push(`graphx_otlp_exports_total{outcome="exported"} ${otlpExporter.stats.exported}`)
  lines.push(`graphx_otlp_exports_total{outcome="failed"} ${otlpExporter.stats.failed}`)
  lines.push(`graphx_otlp_exports_total{outcome="retried"} ${otlpExporter.stats.retried}`)
  lines.push(`graphx_otlp_exports_total{outcome="dropped"} ${otlpExporter.stats.dropped}`)
  lines.push(`graphx_otlp_exports_total{outcome="rejected"} ${otlpExporter.stats.rejected}`)
  lines.push(`graphx_otlp_queue_depth ${otlpExporter.stats.queueDepth}`)
  lines.push(`graphx_otlp_queue_bytes ${otlpExporter.stats.queueBytes}`)
  lines.push('# HELP graphx_node_cpu_percent Process CPU used by a GraphX node as a percentage of one core.')
  lines.push('# TYPE graphx_node_cpu_percent gauge')
  for (const [id, node] of Object.entries(nodes))
    if (Number.isFinite(node.cpuPercent))
      lines.push(`graphx_node_cpu_percent{node="${id}"} ${node.cpuPercent}`)
  for (const [id, edge] of Object.entries(edges)) {
    const label = `edge="${id}"`
    const rates = currentRates(edge)
    lines.push(`graphx_edge_messages_total{${label},direction="sent"} ${edge.sent}`)
    lines.push(`graphx_edge_messages_total{${label},direction="received"} ${edge.received}`)
    lines.push(`graphx_edge_wire_bytes_total{${label},direction="sent"} ${edge.sentWireBytes}`)
    lines.push(`graphx_edge_wire_bytes_total{${label},direction="received"} ${edge.receivedWireBytes}`)
    lines.push(`graphx_edge_message_rate{${label}} ${rates.messageRate}`)
    lines.push(`graphx_edge_wire_byte_rate{${label}} ${rates.byteRate}`)
    lines.push(`graphx_edge_errors_total{${label}} ${edge.errors}`)
    lines.push(`graphx_edge_dropped_total{${label}} ${edge.drops}`)
    lines.push(`graphx_edge_rejected_total{${label}} ${edge.rejected}`)
    lines.push(`graphx_edge_reconnects_total{${label}} ${edge.reconnects}`)
    lines.push(`graphx_edge_backpressure_events_total{${label}} ${edge.backpressureEvents}`)
    lines.push(`graphx_edge_backpressure_seconds_total{${label}} ${edge.backpressureUs / 1e6}`)
    let cumulative = 0
    latencyBoundsUs.forEach((bound, index) => {
      cumulative += edge.latencyBuckets[index]
      lines.push(`graphx_edge_latency_seconds_bucket{${label},le="${bound / 1e6}"} ${cumulative}`)
    })
    cumulative += edge.latencyBuckets.at(-1)
    lines.push(`graphx_edge_latency_seconds_bucket{${label},le="+Inf"} ${cumulative}`)
    lines.push(`graphx_edge_latency_seconds_sum{${label}} ${edge.latencySumUs / 1e6}`)
    lines.push(`graphx_edge_latency_seconds_count{${label}} ${edge.latencyCount}`)
    lines.push(`graphx_edge_connected{${label}} ${edge.connection === 'connected' ? 1 : 0}`)
  }
  return `${lines.join('\n')}\n`
}

const handleRequest = (request, response) => {
  if (!request.url || request.url.length > 2048) return json(response, 414, { error: 'request target too long' })
  const url = parseRequestUrl(request.url)
  if (!url) return json(response, 400, { error: 'invalid request target' })
  if (url.pathname === '/api/live') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, { status: 'live', service: 'graphx-telemetry' })
  }
  if (url.pathname === '/api/ready') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    const ready = serviceState.httpReady && serviceState.udpReady && !serviceState.shuttingDown
    return json(response, ready ? 200 : 503, { status: ready ? 'ready' : 'not-ready', service: 'graphx-telemetry',
      listeners: { http: serviceState.httpReady, udp: serviceState.udpReady }, shuttingDown: serviceState.shuttingDown })
  }
  if (url.pathname === '/api/health') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
    return json(response, 200, { status: 'live', service: 'graphx-telemetry', tls: Boolean(tlsCertificateFile),
      serviceReady: serviceState.httpReady && serviceState.udpReady && !serviceState.shuttingDown,
      graphReady: readiness.ready })
  }
  if (!withinRateLimit(request, 120)) return json(response, 429, { error: 'rate limit exceeded' }, { 'retry-after': '60' })
  const observed = ['/api/topology', '/api/captures', '/api/graph/ready', '/api/slo', '/metrics'].includes(url.pathname) ||
    url.pathname.startsWith('/captures/')
  if (observed && observationToken && !authorized(request, observationToken))
    return json(response, 401, { error: 'invalid observation token' }, { 'www-authenticate': 'Bearer realm="graphx-observation"' })
  if (url.pathname === '/api/topology') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, snapshot())
  }
  if (url.pathname === '/api/graph/ready') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
    return json(response, readiness.ready ? 200 : 503,
      { status: readiness.ready ? 'ready' : 'not-ready', ...readiness })
  }
  if (url.pathname === '/api/slo') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, slo)
  }
  if (url.pathname === '/api/captures') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, snapshot().capture)
  }
  if (url.pathname === '/metrics') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    response.writeHead(200, { ...securityHeaders, 'cache-control': 'no-store',
      'content-type': 'text/plain; version=0.0.4; charset=utf-8' })
    return response.end(prometheus())
  }
  if (url.pathname.startsWith('/api/control/') && request.method === 'POST') {
    const action = url.pathname.split('/').pop()
    if (!requestOriginAllowed(request)) return json(response, 403, { accepted: false, action, error: 'origin not allowed' })
    if (!withinRateLimit(request, 10, 60000, 'control')) return json(response, 429, { accepted: false, action, error: 'control rate limit exceeded' })
    if (Number(request.headers['content-length'] || 0) > 0 || request.headers['transfer-encoding'])
      return json(response, 413, { accepted: false, action, error: 'request body not accepted' })
    if (!controlToken)
      return json(response, 503, { accepted: false, action, error: 'runtime control is disabled' })
    if (!authorized(request, controlToken))
      return json(response, 401, { accepted: false, action, error: 'invalid control token' },
        { 'www-authenticate': 'Bearer realm="graphx-control"' })
    if (action === 'reset') {
      recent.length = 0
      captureReferences.length = 0
      for (const edge of Object.values(edges)) {
        const connection = edge.connection
        Object.assign(edge, emptyEdge(connection))
      }
      state = { ...state, updatedAt: new Date().toISOString() }
      broadcast()
      return json(response, 200, { accepted: true, action, state })
    }
    if (action !== 'pause' && action !== 'resume')
      return json(response, 404, { accepted: false, action, error: 'unknown control action' })
    const endpoints = liveControlEndpoints()
    if (!endpoints.length)
      return json(response, 409, { accepted: false, action, error: 'no live runtime nodes' })
    const command = Buffer.from(JSON.stringify(signEnvelope(
      { kind: 'control', action, timestamp: Date.now() }, telemetrySecret)))
    for (const [, endpoint] of endpoints) udp.send(command, endpoint.port, endpoint.address)
    state = { ...state, paused: action === 'pause', updatedAt: new Date().toISOString() }
    broadcast()
    return json(response, 202, { accepted: true, action, delivered: endpoints.length, state })
  }
  if (url.pathname.startsWith('/api/control/'))
    return json(response, 405, { error: 'method not allowed' }, { allow: 'POST' })
  if (url.pathname.startsWith('/captures/')) {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    let name
    try { name = decodeURIComponent(url.pathname.slice('/captures/'.length)) }
    catch { return json(response, 400, { error: 'invalid capture name' }) }
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}\.pcapng$/.test(name))
      return json(response, 404, { error: 'capture not found' })
    const available = captureFiles().some(file => file.name === name)
    const capturePath = join(captureDirectory, name)
    if (!available || !existsSync(capturePath)) return json(response, 404, { error: 'capture not found' })
    response.writeHead(200, { ...securityHeaders, 'cache-control': 'no-store', 'content-type': 'application/vnd.tcpdump.pcap',
      'content-disposition': `attachment; filename="${name}"` })
    return createReadStream(capturePath).pipe(response)
  }
  let requested = url.pathname === '/' ? 'index.html' : url.pathname.slice(1)
  const file = resolve(root, requested)
  if ((file !== root && !file.startsWith(`${root}/`)) || !existsSync(file)) requested = 'index.html'
  const fallback = normalize(join(root, requested))
  if (!existsSync(fallback)) return json(response, 404, { error: 'web assets not built' })
  response.writeHead(200, { ...securityHeaders, 'cache-control': fallback.endsWith('index.html') ? 'no-cache' : 'public, max-age=3600',
    'content-type': types[extname(fallback)] || 'application/octet-stream' })
  createReadStream(fallback).pipe(response)
}

const requestHandler = (request, response) => {
  try { return handleRequest(request, response) }
  catch (error) {
    console.error(`telemetry request failed: ${error instanceof Error ? error.message : 'unknown error'}`)
    if (!response.headersSent) return json(response, 500, { error: 'internal server error' })
    response.destroy()
  }
}

const serverOptions = { maxHeaderSize: 16 * 1024, requestTimeout: 10000,
  headersTimeout: 10000, keepAliveTimeout: 5000 }
const server = tlsCertificateFile ? createSecureServer({ ...serverOptions,
  cert: readFileSync(tlsCertificateFile), key: readFileSync(tlsPrivateKeyFile), minVersion: 'TLSv1.3',
  ...(tlsClientCaFile ? { ca: readFileSync(tlsClientCaFile), requestCert: true, rejectUnauthorized: true } : {}),
}, requestHandler) : createServer(serverOptions, requestHandler)
server.maxHeadersCount = 64
server.maxRequestsPerSocket = 100
server.on('clientError', (_error, socket) => {
  if (socket.writable) socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n')
})

const webSockets = new WebSocketServer({ noServer: true, maxPayload: 4096,
  handleProtocols: protocols => protocols.has('graphx') ? 'graphx' : false })
server.on('upgrade', (request, socket, head) => {
  try {
    const url = parseRequestUrl(request.url || '/')
    if (!url) return socket.destroy()
    const protocols = (request.headers['sec-websocket-protocol'] || '').split(',').map(value => value.trim()).filter(Boolean)
    const supplied = webSocketBearer(protocols, observationToken)
    if (url.pathname !== websocketPath || !requestOriginAllowed(request) || !withinRateLimit(request, 120) ||
        (observationToken && !tokenMatches(observationToken, `Bearer ${supplied}`))) return socket.destroy()
    webSockets.handleUpgrade(request, socket, head, websocket => webSockets.emit('connection', websocket, request))
  } catch { socket.destroy() }
})
webSockets.on('connection', socket => socket.send(JSON.stringify(snapshot())))
function broadcast() {
  const message = JSON.stringify(snapshot())
  for (const socket of webSockets.clients) if (socket.readyState === 1) socket.send(message)
}

const udp = dgram.createSocket('udp4')
const replayCache = new ReplayCache()
const nodeIds = new Set(Object.keys(nodes))
const edgeIds = new Set(Object.keys(edges))
udp.on('message', (data, remote) => {
  try {
    if (data.length > MAX_DATAGRAM_BYTES) return
    const event = verifyEnvelope(JSON.parse(data.toString('utf8')), telemetrySecret, replayCache)
    if (!validateTelemetryEvent(event, nodeIds, edgeIds)) return
    if (configuredOtlp.enabled && event.kind === 'trace')
      otlpExporter.enqueue(configuredOtlp.tracesPath, otlpTraceRequest(event))
    const receivedAt = Date.now()
    if (event.kind === 'control_ack' && nodes[event.nodeId] &&
        (event.action === 'pause' || event.action === 'resume')) {
      controlAcks.set(event.nodeId, { action: event.action,
        accepted: event.accepted === true, receivedAt })
      broadcast()
      return
    }
    if (event.kind === 'capture' && event.event === 'frame') {
      captureReferences.unshift(event)
      if (captureReferences.length > 200) captureReferences.length = 200
      broadcast()
      return
    }
    if (nodes[event.nodeId]) {
      controlEndpoints.set(event.nodeId, { address: remote.address, port: remote.port,
        lastSeen: receivedAt })
      const cpuPercent = Number(event.cpuPercent)
      nodes[event.nodeId] = {
        ...nodes[event.nodeId], status: 'running', lastSeen: receivedAt,
        ...(Number.isFinite(cpuPercent) && cpuPercent >= 0 ? { cpuPercent } : {}),
      }
    }
    const edge = edges[event.edgeId]
    if (edge) {
      edge.lastSeen = receivedAt
      edge.lastSequence = event.sequence || edge.lastSequence
      if (event.event === 'send') {
        // A live data event is authoritative evidence that the transport path is
        // usable, including after the telemetry service itself has restarted.
        edge.connection = 'connected'
        const wireBytes = Math.max(0, Number(event.wireBytes) || 0)
        edge.sent += 1
        edge.sentWireBytes += wireBytes
        recordRate(edge, receivedAt, wireBytes)
      }
      if (event.event === 'receive') {
        edge.connection = 'connected'
        edge.received += 1
        edge.receivedWireBytes += Math.max(0, Number(event.wireBytes) || 0)
        const latencyUs = Math.max(0, Number(event.latencyUs) || 0)
        edge.latencyCount += 1
        edge.latencySumUs += latencyUs
        let bucket = latencyBoundsUs.findIndex(bound => latencyUs <= bound)
        if (bucket < 0) bucket = latencyBoundsUs.length
        edge.latencyBuckets[bucket] += 1
        recent.unshift(event)
        if (recent.length > 100) recent.length = 100
      }
      if (event.event === 'error') { edge.errors += 1; edge.connection = 'error' }
      if (event.event === 'connection') edge.connection = event.message || 'unknown'
      if (event.event === 'reconnect') edge.reconnects += 1
      if (event.event === 'backpressure') {
        edge.backpressureEvents += 1
        edge.backpressureUs += event.latencyUs || 0
        if (event.message === 'rejected') {
          edge.rejected += 1
          edge.drops += 1
        }
      }
      if (event.event === 'drop') edge.drops += 1
    }
    broadcast()
  } catch { /* Telemetry is best-effort; malformed datagrams are ignored. */ }
})

udp.on('listening', () => { serviceState.udpReady = true })
udp.on('close', () => { serviceState.udpReady = false })
udp.bind(udpPort, udpBind)
const healthTimer = setInterval(() => {
  const now = Date.now()
  let changed = false
  for (const node of Object.values(nodes)) {
    if (node.lastSeen && now - node.lastSeen > heartbeatTimeout && node.status !== 'offline') {
      node.status = 'offline'
      changed = true
    }
  }
  if (changed) broadcast()
}, Math.max(250, Math.min(heartbeatTimeout / 2, 1000))).unref()
const sloTimer = setInterval(() => {
  const timestamp = Date.now()
  const readiness = graphReadiness(nodes, edges, timestamp, heartbeatTimeout)
  slo = sloEvaluator.observe(readiness.ready, edges, timestamp)
}, 1000).unref()
const otlpTimer = setInterval(() => {
  if (configuredOtlp.enabled) {
    const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
    otlpExporter.enqueue(configuredOtlp.metricsPath, otlpMetricsRequest(
      Object.fromEntries(Object.entries(edges).map(([id, edge]) => [id, edgeView(edge)])), nodes, slo,
      readiness.ready))
  }
}, configuredOtlp.exportIntervalMs).unref()
server.listen(port, httpBind, () => {
  serviceState.httpReady = true
  console.log(`GraphX telemetry ${tlsCertificateFile ? 'HTTPS/WSS' : 'HTTP/WS'} ${httpBind}:${port}, UDP ${udpBind}:${udpPort}`)
})

function shutdown() {
  if (serviceState.shuttingDown) return
  serviceState.shuttingDown = true
  serviceState.httpReady = false
  clearInterval(healthTimer); clearInterval(sloTimer); clearInterval(otlpTimer)
  otlpExporter.close()
  for (const socket of webSockets.clients) socket.close(1001, 'service shutting down')
  webSockets.close()
  udp.close()
  server.close(() => process.exit(0))
  setTimeout(() => process.exit(1), 5000).unref()
}
process.on('SIGTERM', shutdown)
process.on('SIGINT', shutdown)
