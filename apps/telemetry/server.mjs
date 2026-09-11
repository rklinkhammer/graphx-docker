import dgram from 'node:dgram'
import { existsSync, readFileSync } from 'node:fs'
import { createServer } from 'node:http'
import { createServer as createSecureServer } from 'node:https'
import { join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocketServer } from 'ws'
import { loadTelemetryConfiguration } from './normalized-config.mjs'
import { MAX_DATAGRAM_BYTES, RateLimiter, ReplayCache, isLoopback, originAllowed,
  parseRequestUrl, readSecret, sanitizeControlAcknowledgement, sanitizeTelemetryEvent,
  signEnvelope, tokenMatches, validateTelemetryEvent, verifyEnvelope, webSocketBearer } from './security.mjs'
import { OtlpHttpExporter, SloEvaluator, graphReadiness, otlpConfig, otlpMetricsRequest,
  otlpTraceRequest } from './operations.mjs'
import { HistoryStore, historyConfig, sloHistoryRecord, telemetryHistoryRecord } from './history.mjs'
import { ControlAuthorizer, ControlConflictError, ControlPlane, CredentialRegistry,
  PreviousCredentialStore, RuntimeIdentityStore, controlAuditHistoryRecord,
  controlConfig } from './control.mjs'
import { listValidatedCaptures } from './capture-files.mjs'
import { createTopology, topologyView } from './topology.mjs'
import { createRuntimeEvidence, diagnosticLayerByState } from './runtime-evidence.mjs'
import { createMetricStore, currentRates, edgeView, LATENCY_BOUNDS_US,
  RATE_WINDOW_SECONDS } from './metric-store.mjs'
import { createHttpRequestHandler } from './http-routes.mjs'

const root = normalize(process.env.GRAPHX_WEB_ROOT || join(fileURLToPath(new URL('.', import.meta.url)), '../../web/dist'))
const port = Number(process.env.PORT || 8080)
const udpPort = Number(process.env.GRAPHX_TELEMETRY_PORT || 9000)
const httpBind = process.env.GRAPHX_HTTP_BIND || '127.0.0.1'
const udpBind = process.env.GRAPHX_TELEMETRY_BIND || '127.0.0.1'
const fallbackConfigPath = normalize(join(fileURLToPath(new URL('.', import.meta.url)), '../../graphx.yaml'))
const loadedConfiguration = loadTelemetryConfiguration({ fallbackPath: fallbackConfigPath })
const config = loadedConfiguration.config
const graph = config.graph || { id: 'graphx', nodes: [], edges: [] }
const packetHistoryUrl = process.env.GRAPHX_PACKET_HISTORY_URL || ''
const qemuEvidenceFile = process.env.GRAPHX_QEMU_EVIDENCE_FILE || ''
const networkDiagnosticFile = process.env.GRAPHX_NETWORK_DIAGNOSTIC_FILE || ''
const heartbeatTimeout = Number(process.env.GRAPHX_HEARTBEAT_TIMEOUT_MS || config.observability?.telemetry?.heartbeat_timeout_ms || 5000)
const websocketPath = config.observability?.telemetry?.websocket || '/ws'
const configuredCapture = config.observability?.capture || { enabled: false, provider: '' }
function captureInteger(name, configured, fallback, minimum, maximum) {
  const fromEnvironment = process.env[name]
  const candidate = fromEnvironment ?? configured ?? fallback
  if (fromEnvironment == null && typeof candidate !== 'number')
    throw new Error(`${name} must be a typed integer between ${minimum} and ${maximum}`)
  if (fromEnvironment != null && !/^[0-9]+$/.test(candidate))
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}`)
  const value = Number(candidate)
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum)
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}`)
  return value
}
function captureBoolean(name, configured, fallback) {
  const candidate = process.env[name]
  if (candidate == null) {
    const value = configured ?? fallback
    if (typeof value !== 'boolean') throw new Error(`${name} must be a typed boolean`)
    return value
  }
  const value = candidate.toLowerCase()
  if (['1', 'true', 'yes', 'on'].includes(value)) return true
  if (['0', 'false', 'no', 'off'].includes(value)) return false
  throw new Error(`${name} must be one of true, false, 1, 0, yes, no, on, or off`)
}
const sloEvaluator = new SloEvaluator(config.observability?.slos)
const configuredOtlp = otlpConfig(config.observability?.otlp)
const otlpExporter = new OtlpHttpExporter(configuredOtlp)
const configuredHistory = historyConfig(config.observability?.history, process.env,
  loadedConfiguration.baseDirectory)
const historyStore = new HistoryStore(configuredHistory, graph.id)
const configuredControl = controlConfig(config.observability?.control)
const captureConfig = { ...configuredCapture,
  enabled: captureBoolean('GRAPHX_CAPTURE_ENABLED', configuredCapture.enabled, false),
  provider: process.env.GRAPHX_CAPTURE_PROVIDER || configuredCapture.provider || '',
  snaplen: captureInteger('GRAPHX_CAPTURE_SNAPLEN', configuredCapture.snaplen,
    16 * 1024 * 1024 + 4, 256, 16 * 1024 * 1024 + 4),
  maxFileBytes: captureInteger('GRAPHX_CAPTURE_MAX_FILE_BYTES', configuredCapture.max_file_bytes,
    256 * 1024 * 1024, 65536, 4 * 1024 * 1024 * 1024),
  maxPackets: captureInteger('GRAPHX_CAPTURE_MAX_PACKETS', configuredCapture.max_packets,
    1_000_000, 1, 100_000_000),
}
if (captureConfig.provider && !['pcapng', 'ovs-span'].includes(captureConfig.provider))
  throw new Error('GRAPHX_CAPTURE_PROVIDER must be pcapng or ovs-span')
if (captureConfig.enabled && !captureConfig.provider)
  throw new Error('GRAPHX_CAPTURE_PROVIDER is required when capture is enabled')
if (captureConfig.enabled && captureConfig.provider === 'pcapng' &&
    !process.env.GRAPHX_CAPTURE_DIR && !configuredCapture.directory)
  throw new Error('GRAPHX_CAPTURE_DIR is required for the pcapng provider')
const captureDirectory = resolve(process.env.GRAPHX_CAPTURE_DIR || captureConfig.directory || 'captures')
const captureCatalogMaxFiles = captureInteger('GRAPHX_CAPTURE_CATALOG_MAX_FILES', undefined,
  128, 1, 1024)
const captureCatalogMaxEntries = captureInteger('GRAPHX_CAPTURE_CATALOG_MAX_ENTRIES', undefined,
  512, captureCatalogMaxFiles, 4096)
const controlToken = readSecret('GRAPHX_CONTROL_TOKEN')
const controlPolicyFile = process.env.GRAPHX_CONTROL_POLICY_FILE || ''
const runtimeIdentityFile = process.env.GRAPHX_RUNTIME_IDENTITY_FILE || ''
const previousCredentialFile = process.env.GRAPHX_PREVIOUS_CREDENTIALS_FILE || ''
const observationToken = readSecret('GRAPHX_OBSERVATION_TOKEN')
const telemetrySecret = readSecret('GRAPHX_TELEMETRY_SHARED_SECRET')
const tlsCertificateFile = process.env.GRAPHX_TLS_CERT_FILE || ''
const tlsPrivateKeyFile = process.env.GRAPHX_TLS_KEY_FILE || ''
const tlsClientCaFile = process.env.GRAPHX_TLS_CLIENT_CA_FILE || ''
if (Boolean(tlsCertificateFile) !== Boolean(tlsPrivateKeyFile))
  throw new Error('GRAPHX_TLS_CERT_FILE and GRAPHX_TLS_KEY_FILE must be provided together')
if (controlToken && controlPolicyFile)
  throw new Error('GRAPHX_CONTROL_TOKEN and GRAPHX_CONTROL_POLICY_FILE are mutually exclusive')
if (runtimeIdentityFile && telemetrySecret)
  throw new Error('GRAPHX_RUNTIME_IDENTITY_FILE and GRAPHX_TELEMETRY_SHARED_SECRET are mutually exclusive')
if (controlPolicyFile && !runtimeIdentityFile)
  throw new Error('GRAPHX_RUNTIME_IDENTITY_FILE is required with GRAPHX_CONTROL_POLICY_FILE')
if (controlToken && !telemetrySecret)
  throw new Error('GRAPHX_TELEMETRY_SHARED_SECRET is required with legacy runtime control')
if (!tlsCertificateFile && !isLoopback(httpBind) && process.env.GRAPHX_ALLOW_INSECURE_REMOTE !== 'true')
  throw new Error('plaintext telemetry may bind only to loopback; use TLS or explicitly set GRAPHX_ALLOW_INSECURE_REMOTE=true')
const allowedOrigins = new Set((process.env.GRAPHX_ALLOWED_ORIGINS || '').split(',').map(v => v.trim()).filter(Boolean))
const rateWindowSeconds = RATE_WINDOW_SECONDS
const latencyBoundsUs = LATENCY_BOUNDS_US

const topology = createTopology(config)
const { qemuEvidence, networkDiagnosticEvidence } = createRuntimeEvidence({
  qemuEvidenceFile, networkDiagnosticFile, topology,
})
let state = { paused: false, fault: false, updatedAt: new Date().toISOString() }
const controlEndpoints = new Map()
const controlStates = new Map()
const metricStore = createMetricStore(topology)
const { nodes, edges } = metricStore
const nodeIds = new Set(Object.keys(nodes))
const edgeIds = new Set(Object.keys(edges))
const controllableNodeIds = new Set(graph.nodes.filter(node =>
  node.control === 'origin' ||
  ((node.control === 'graphx' || node.control == null) && node.kind === 'source')).map(node => node.id))
const runtimeIdentities = new RuntimeIdentityStore({ manifestFile: runtimeIdentityFile, nodeIds })
const controlAuthorizer = new ControlAuthorizer({ policyFile: controlPolicyFile,
  legacyToken: controlToken, nodeIds })
const previousCredentials = new PreviousCredentialStore({ manifestFile: previousCredentialFile })
const credentialRegistry = new CredentialRegistry({ observationToken, telemetrySecret,
  controlAuthorizer, runtimeIdentities, previousCredentials })
let reportedCredentialError = null
function refreshCredentials(force = false) {
  const valid = credentialRegistry.reload(force)
  if (credentialRegistry.lastError !== reportedCredentialError) {
    if (credentialRegistry.lastError) {
      controlEndpoints.clear()
      controlStates.clear()
      console.error(`GraphX credential configuration invalid: ${credentialRegistry.lastError}`)
    } else if (reportedCredentialError)
      console.log('GraphX credential configuration recovered')
    reportedCredentialError = credentialRegistry.lastError
  }
  return valid
}
refreshCredentials(true)
const controlPlane = new ControlPlane(configuredControl, controllableNodeIds, {
  auditSink: (entry, recordedAt) => {
    if (configuredHistory.enabled &&
        !historyStore.enqueue(controlAuditHistoryRecord(entry, graph.id, recordedAt)))
      throw new Error('control audit history queue rejected record')
  },
})
let serviceState = { httpReady: false, udpReady: false, shuttingDown: false }
let slo = sloEvaluator.snapshot()
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png' }

let captureCatalogCache = { expiresAt: 0, catalog: null }
function captureCatalog() {
  if (!captureConfig.enabled || !existsSync(captureDirectory))
    return { files: [], scannedEntries: 0, truncated: false }
  const now = Date.now()
  if (captureCatalogCache.catalog && captureCatalogCache.expiresAt > now)
    return captureCatalogCache.catalog
  const listed = listValidatedCaptures(captureDirectory, captureConfig.maxFileBytes,
    { maxFiles: captureCatalogMaxFiles, maxEntries: captureCatalogMaxEntries,
      maxBlocks: captureConfig.maxPackets + 2 })
  const catalog = { scannedEntries: listed.scannedEntries, truncated: listed.truncated,
    files: listed.captures.map(({ name, details, linkType }) => ({
      name, nodeId: name.slice(0, -7), size: details.size,
      modifiedAt: details.mtime.toISOString(), url: `/captures/${encodeURIComponent(name)}`,
      linkType, format: linkType === 1 ? 'ethernet' : 'graphx-frame',
    })) }
  captureCatalogCache = { expiresAt: now + 1000, catalog }
  return catalog
}

function snapshot() {
  refreshCredentials()
  const timestamp = Date.now()
  const edgeViews = Object.fromEntries(
    Object.entries(edges).map(([id, edge]) => [id, edgeView(edge, timestamp)]))
  const liveEndpoints = [...controlEndpoints.values()].filter(endpoint =>
    timestamp - endpoint.lastSeen <= heartbeatTimeout)
  const catalog = captureCatalog()
  const evidence = qemuEvidence()
  const diagnosticEvidence = networkDiagnosticEvidence()
  const nodeViews = { ...nodes }
  const qemuNode = topology.nodes.find(node => node.runtime === 'qemu')
  if (qemuNode && evidence) nodeViews[qemuNode.id] = { ...nodeViews[qemuNode.id],
    status: evidence.state, runtimeEvidenceAt: evidence.updatedAt || null }
  const readiness = graphReadiness(nodes, edges, timestamp, heartbeatTimeout)
  if (diagnosticEvidence)
    for (const [edgeId, flow] of Object.entries(diagnosticEvidence.flows))
      if (edgeViews[edgeId]) edgeViews[edgeId] = { ...edgeViews[edgeId],
        connection: flow.state, diagnosticEvidence: flow.evidence,
        diagnosticLayer: diagnosticLayerByState.get(flow.state) }
  return { kind: 'snapshot', graph: graph.id,
    topology: topologyView(topology, evidence, diagnosticEvidence, diagnosticLayerByState),
    telemetry: { websocket: websocketPath, heartbeatTimeoutMs: heartbeatTimeout,
      rateWindowSeconds, latencyBoundsUs }, state,
    control: { available: credentialRegistry.lastError == null && controlAuthorizer.available &&
        (runtimeIdentities.available || telemetrySecret.length > 0),
      authenticatedTelemetry: credentialRegistry.lastError == null &&
        (runtimeIdentities.available || telemetrySecret.length > 0),
      nodeBoundIdentity: runtimeIdentities.available, connectedNodes: liveEndpoints.length,
      controllableNodes: [...controllableNodeIds], nodeStates: Object.fromEntries(controlStates),
      pendingCommands: controlPlane.pendingCount(), stats: { ...controlPlane.stats },
      policyValid: credentialRegistry.lastError == null },
    health: { serviceReady: serviceState.httpReady && serviceState.udpReady &&
        !serviceState.shuttingDown && credentialRegistry.lastError == null,
      graph: readiness }, slo, otlp: { enabled: configuredOtlp.enabled, ...otlpExporter.stats },
    history: { ...historyStore.stats },
    packetHistory: { enabled: Boolean(packetHistoryUrl), backend: 'sqlite',
      status: packetHistoryUrl ? 'configured' : 'disabled' },
    nodes: nodeViews, edges: edgeViews,
    capture: { enabled: Boolean(captureConfig.enabled), provider: captureConfig.provider || null,
      format: catalog.files.length ? 'pcapng' : null,
      limits: { snaplen: captureConfig.snaplen, maxFileBytes: captureConfig.maxFileBytes,
        maxPackets: captureConfig.maxPackets, catalogMaxFiles: captureCatalogMaxFiles,
        catalogMaxEntries: captureCatalogMaxEntries },
      files: catalog.files, catalogTruncated: catalog.truncated,
      catalogScannedEntries: catalog.scannedEntries }, recent: metricStore.recentWithCapture(), timestamp }
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

function controlPrincipal(request) {
  refreshCredentials()
  return controlAuthorizer.authenticate(request.headers.authorization || '', false)
}

function controlTargets(action, requested = null) {
  if (action === 'reset') return ['collector']
  const targets = requested == null ? [...controllableNodeIds] : requested
  if (!Array.isArray(targets) || !targets.length || targets.length > nodeIds.size ||
      targets.some(node => typeof node !== 'string' || !controllableNodeIds.has(node)))
    throw new Error('targetNodes must contain configured source node identifiers')
  return [...new Set(targets)]
}

function resetCollectorCounters() {
  metricStore.reset()
  state = { ...state, updatedAt: new Date().toISOString() }
  broadcast()
}

function deliverControl(command) {
  if (command.action === 'reset') { resetCollectorCounters(); return 1 }
  let delivered = 0
  const endpoints = new Map(liveControlEndpoints())
  for (const nodeId of command.targetNodes) {
    const endpoint = endpoints.get(nodeId)
    if (!endpoint) continue
    const payload = { kind: 'control', action: command.action, commandId: command.id,
      targetNode: nodeId, issuedAt: command.issuedAt, expiresAt: command.expiresAt }
    const targetSecret = runtimeIdentities.available ? runtimeIdentities.secretFor(nodeId, false) : telemetrySecret
    if (!targetSecret) continue
    const datagram = Buffer.from(JSON.stringify(signEnvelope(payload, targetSecret)))
    udp.send(datagram, endpoint.port, endpoint.address)
    delivered++
  }
  return delivered
}

function authorizeControl(request, response, action, targets) {
  const principal = controlPrincipal(request)
  if (!principal) {
    controlPlane.deny({ action, targets, reason: 'authentication failed' })
    json(response, 401, { accepted: false, action, error: 'invalid control credential' },
      { 'www-authenticate': 'Bearer realm="graphx-control"' })
    return null
  }
  if (!controlAuthorizer.permits(principal, action, targets)) {
    controlPlane.deny({ actor: principal.id, action, targets, reason: 'authorization policy denied action or target' })
    json(response, 403, { accepted: false, action, error: 'control action or target is not authorized' })
    return null
  }
  return principal
}

function issueControl(request, response, { action, targetNodes, reason = null }) {
  if (!['pause', 'resume', 'reset'].includes(action))
    return json(response, 400, { accepted: false, action, error: 'unknown control action' })
  if (!refreshCredentials() || !controlAuthorizer.available ||
      (!runtimeIdentities.available && !telemetrySecret))
    return json(response, 503, { accepted: false, action, error: 'runtime control is disabled' })
  let targets
  try { targets = controlTargets(action, targetNodes) }
  catch (error) { return json(response, 400, { accepted: false, action, error: error.message }) }
  const principal = authorizeControl(request, response, action, targets)
  if (!principal) return
  if (reason != null && credentialRegistry.credentialValues()
    .some(secret => reason.includes(secret)))
    return json(response, 400, { accepted: false, action,
      error: 'control reason must not contain a configured credential' })
  try {
    const result = controlPlane.issue({ action, targetNodes: targets, actor: principal.id, reason,
      idempotencyKey: request.headers['idempotency-key'] || null }, deliverControl)
    if (result.command.status === 'accepted')
      state = { ...state, paused: action === 'pause' ? true : action === 'resume' ? false : state.paused,
        updatedAt: new Date().toISOString() }
    return json(response, action === 'reset' ? 200 : 202,
      { accepted: true, replayed: result.replayed, command: result.command, state })
  } catch (error) {
    const status = error instanceof ControlConflictError ? 409 :
      error.message.includes('capacity') ? 429 :
      error.message.includes('no live') ? 409 : 400
    controlPlane.record({ actor: principal.id, action, targets, decision: 'rejected',
      reason: String(error.message).slice(0, 256) })
    return json(response, status, { accepted: false, action, error: error.message },
      status === 429 ? { 'retry-after': '1' } : {})
  }
}

function readControlBody(request) {
  return new Promise((resolveBody, rejectBody) => {
    const declared = Number(request.headers['content-length'] || 0)
    if (!Number.isSafeInteger(declared) || declared < 0 || declared > configuredControl.maxRequestBytes)
      return rejectBody(new Error('control request body exceeds configured limit'))
    let size = 0
    let settled = false
    const chunks = []
    request.on('data', chunk => {
      if (settled) return
      size += chunk.length
      if (size > configuredControl.maxRequestBytes) {
        settled = true
        rejectBody(new Error('control request body exceeds configured limit'))
      } else chunks.push(chunk)
    })
    request.on('end', () => {
      if (settled) return
      try {
        const parsed = JSON.parse(Buffer.concat(chunks).toString('utf8'))
        if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed) ||
            Object.keys(parsed).some(key => !['action', 'targetNodes', 'reason'].includes(key)))
          throw new Error('control request has unknown or invalid properties')
        settled = true
        resolveBody(parsed)
      } catch (error) { settled = true; rejectBody(error) }
    })
    request.on('error', rejectBody)
  })
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
  refreshCredentials()
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
    '# HELP graphx_service_ready Whether listeners are ready and credential configuration is valid.',
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
    '# HELP graphx_history_backend_up Whether the optional durable history backend is ready.',
    '# TYPE graphx_history_backend_up gauge',
    '# HELP graphx_history_enabled Whether durable history is configured.',
    '# TYPE graphx_history_enabled gauge',
    '# HELP graphx_history_records_total Durable history record outcomes.',
    '# TYPE graphx_history_records_total counter',
    '# HELP graphx_history_queue_depth Current bounded durable-history write queue depth.',
    '# TYPE graphx_history_queue_depth gauge',
    '# HELP graphx_history_queue_bytes Current bounded durable-history write queue bytes.',
    '# TYPE graphx_history_queue_bytes gauge',
    '# HELP graphx_history_database_bytes Current SQLite database, WAL, and shared-memory bytes.',
    '# TYPE graphx_history_database_bytes gauge',
    '# HELP graphx_control_commands_total Authorized control commands by terminal outcome.',
    '# TYPE graphx_control_commands_total counter',
    '# HELP graphx_control_denied_total Control requests denied by authentication or authorization.',
    '# TYPE graphx_control_denied_total counter',
    '# HELP graphx_control_pending_commands Current commands awaiting runtime acknowledgements.',
    '# TYPE graphx_control_pending_commands gauge',
    '# HELP graphx_control_policy_valid Whether the configured authorization policy is loaded.',
    '# TYPE graphx_control_policy_valid gauge',
    '# HELP graphx_control_audit_dropped_total Audit records dropped from a sink or bounded memory.',
    '# TYPE graphx_control_audit_dropped_total counter',
  ]
  const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
  lines.push(`graphx_service_ready ${serviceState.httpReady && serviceState.udpReady &&
    !serviceState.shuttingDown && credentialRegistry.lastError == null ? 1 : 0}`)
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
  lines.push(`graphx_history_backend_up ${historyStore.stats.status === 'ready' ? 1 : 0}`)
  lines.push(`graphx_history_enabled ${configuredHistory.enabled ? 1 : 0}`)
  for (const outcome of ['written', 'failed', 'dropped', 'pruned'])
    lines.push(`graphx_history_records_total{outcome="${outcome}"} ${historyStore.stats[outcome]}`)
  lines.push(`graphx_history_queue_depth ${historyStore.stats.queueDepth}`)
  lines.push(`graphx_history_queue_bytes ${historyStore.stats.queueBytes}`)
  lines.push(`graphx_history_database_bytes ${historyStore.stats.databaseBytes}`)
  lines.push(`graphx_control_commands_total{outcome="issued"} ${controlPlane.stats.issued}`)
  lines.push(`graphx_control_commands_total{outcome="accepted"} ${controlPlane.stats.accepted}`)
  lines.push(`graphx_control_commands_total{outcome="rejected"} ${controlPlane.stats.rejected}`)
  lines.push(`graphx_control_commands_total{outcome="timed_out"} ${controlPlane.stats.timedOut}`)
  lines.push(`graphx_control_denied_total ${controlPlane.stats.denied}`)
  lines.push(`graphx_control_pending_commands ${controlPlane.pendingCount()}`)
  lines.push(`graphx_control_policy_valid ${credentialRegistry.lastError == null ? 1 : 0}`)
  lines.push(`graphx_control_audit_dropped_total ${controlPlane.stats.auditDropped}`)
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

const requestHandler = createHttpRequestHandler({ json, withinRateLimit, observationToken,
  authorized, snapshot, graphReadiness, nodes, edges, heartbeatTimeout, getSlo: () => slo,
  historyStore, configuredHistory, nodeIds, edgeIds, packetHistoryUrl, securityHeaders,
  prometheus, controlPrincipal, controlPlane, requestOriginAllowed, readControlBody, issueControl,
  captureDirectory, captureConfig, root, types, serviceState, credentialRegistry,
  refreshCredentials, tlsEnabled: Boolean(tlsCertificateFile) })
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
udp.on('message', (data, remote) => {
  try {
    if (data.length > MAX_DATAGRAM_BYTES) return
    const envelope = JSON.parse(data.toString('utf8'))
    if (!refreshCredentials()) return
    const claimedNode = envelope?.payload?.nodeId
    const nodeSecret = runtimeIdentities.available
      ? runtimeIdentities.secretFor(claimedNode, false) : telemetrySecret
    if (runtimeIdentityFile && !nodeSecret) return
    const verifiedEvent = verifyEnvelope(envelope, nodeSecret, replayCache)
    if (!validateTelemetryEvent(verifiedEvent, nodeIds, edgeIds)) return
    // A valid event may arrive immediately after a projected token or identity
    // file changes. Re-read the bounded credential set before any retained or
    // exported value is produced so the candidate and superseded values are in
    // the registry's redaction overlap. Invalid replacements fail closed.
    if (!refreshCredentials(true)) return
    const event = sanitizeTelemetryEvent(sanitizeControlAcknowledgement(verifiedEvent),
      credentialRegistry.credentialValues())
    const receivedAt = Date.now()
    if (event.kind === 'control_ack') {
      const endpoint = controlEndpoints.get(event.nodeId)
      if (!endpoint || endpoint.address !== remote.address || endpoint.port !== remote.port ||
          receivedAt - endpoint.lastSeen > heartbeatTimeout) return
      if (controlPlane.acknowledge(event, receivedAt)) {
        if (configuredHistory.enabled)
          historyStore.enqueue(telemetryHistoryRecord(event, graph.id, receivedAt))
        const command = controlPlane.get(event.commandId)
        if (event.accepted && event.state) controlStates.set(event.nodeId, event.state)
        if (command?.status === 'accepted') {
          const knownStates = [...controllableNodeIds].map(node => controlStates.get(node))
          state = { ...state, paused: knownStates.length > 0 && knownStates.every(value => value === 'paused'),
            updatedAt: new Date().toISOString() }
        }
      }
      broadcast()
      return
    }
    if (configuredHistory.enabled && event.kind !== 'network_packet')
      historyStore.enqueue(telemetryHistoryRecord(event, graph.id, receivedAt))
    if (configuredOtlp.enabled && event.kind === 'trace')
      otlpExporter.enqueue(configuredOtlp.tracesPath, otlpTraceRequest(event))
    if (event.kind === 'capture' && event.event === 'frame') {
      metricStore.recordCapture(event)
      broadcast()
      return
    }
    if (event.kind === 'trace' && nodes[event.nodeId] && controllableNodeIds.has(event.nodeId))
      controlEndpoints.set(event.nodeId, { address: remote.address, port: remote.port,
        lastSeen: receivedAt })
    metricStore.ingest(event, receivedAt)
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
let networkDiagnosticSignature = JSON.stringify(networkDiagnosticEvidence())
const networkDiagnosticTimer = networkDiagnosticFile ? setInterval(() => {
  const next = JSON.stringify(networkDiagnosticEvidence())
  if (next !== networkDiagnosticSignature) {
    networkDiagnosticSignature = next
    broadcast()
  }
}, 500).unref() : null
const sloTimer = setInterval(() => {
  const timestamp = Date.now()
  const readiness = graphReadiness(nodes, edges, timestamp, heartbeatTimeout)
  slo = sloEvaluator.observe(readiness.ready, edges, timestamp)
  if (configuredHistory.enabled)
    historyStore.enqueue(sloHistoryRecord(slo, readiness, graph.id, timestamp))
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
if (configuredHistory.enabled)
  historyStore.waitUntilReady().then(() => console.log('GraphX durable history sqlite backend ready'))
    .catch(error => console.error(`GraphX durable history unavailable: ${String(error.message).slice(0, 256)}`))

function shutdown() {
  if (serviceState.shuttingDown) return
  serviceState.shuttingDown = true
  serviceState.httpReady = false
  clearInterval(healthTimer); clearInterval(sloTimer); clearInterval(otlpTimer)
  if (networkDiagnosticTimer) clearInterval(networkDiagnosticTimer)
  otlpExporter.close()
  for (const socket of webSockets.clients) socket.close(1001, 'service shutting down')
  webSockets.close()
  udp.close()
  let httpClosed = false
  let historyClosed = !configuredHistory.enabled
  let shutdownExitCode = 0
  const finish = () => { if (httpClosed && historyClosed) process.exit(shutdownExitCode) }
  server.close(() => { httpClosed = true; finish() })
  historyStore.close(configuredHistory.shutdownTimeoutMs)
    .catch(error => {
      shutdownExitCode = 1
      console.error(`GraphX durable history shutdown failed: ${String(error.message).slice(0, 256)}`)
    })
    .finally(() => { historyClosed = true; finish() })
  setTimeout(
    () => process.exit(1),
    Math.max(5000, configuredHistory.shutdownTimeoutMs + 1000)
  ).unref()
}
process.on('SIGTERM', shutdown)
process.on('SIGINT', shutdown)
