import dgram from 'node:dgram'
import { readFileSync } from 'node:fs'
import { createServer } from 'node:http'
import { createServer as createSecureServer } from 'node:https'
import { join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocketServer } from 'ws'
import { loadTelemetryConfiguration } from './normalized-config.mjs'
import { isLoopback, readSecret } from './security.mjs'
import { OtlpHttpExporter, SloEvaluator, graphReadiness, otlpConfig } from './operations.mjs'
import { HistoryStore, historyConfig } from './history.mjs'
import { controlConfig } from './control.mjs'
import { createTopology } from './topology.mjs'
import { createRuntimeEvidence } from './runtime-evidence.mjs'
import { createHttpRequestHandler } from './http-routes.mjs'
import { createTelemetryCollector } from './collector.mjs'

const root = normalize(process.env.GRAPHX_WEB_ROOT || join(fileURLToPath(new URL('.', import.meta.url)), '../../web/dist'))
const port = Number(process.env.PORT || 8080)
const udpPort = Number(process.env.GRAPHX_TELEMETRY_PORT || 9000)
const httpBind = process.env.GRAPHX_HTTP_BIND || '127.0.0.1'
const udpBind = process.env.GRAPHX_TELEMETRY_BIND || '127.0.0.1'
const loadedConfiguration = loadTelemetryConfiguration()
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
const topology = createTopology(config)
const runtimeEvidence = createRuntimeEvidence({ qemuEvidenceFile, networkDiagnosticFile, topology })
let udp
const collector = createTelemetryCollector({ graph, topology, websocketPath, heartbeatTimeout,
  configuredOtlp, otlpExporter, configuredHistory, historyStore, configuredControl, sloEvaluator,
  captureConfig, captureDirectory, captureCatalogMaxFiles, captureCatalogMaxEntries,
  packetHistoryUrl, ...runtimeEvidence, runtimeIdentityFile, controlPolicyFile,
  previousCredentialFile, controlToken, observationToken, telemetrySecret, allowedOrigins,
  tlsEnabled: Boolean(tlsCertificateFile), networkDiagnosticEnabled: Boolean(networkDiagnosticFile),
  sendDatagram: (...arguments_) => udp.send(...arguments_) })
const { serviceState, snapshot } = collector
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png' }
const requestHandler = createHttpRequestHandler({ ...collector, graphReadiness, historyStore,
  configuredHistory, packetHistoryUrl, captureDirectory, captureConfig, root, types,
  securityHeaders: collector.securityHeaders, tlsEnabled: Boolean(tlsCertificateFile) })
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
    if (!collector.authorizeWebSocket(request)) return socket.destroy()
    webSockets.handleUpgrade(request, socket, head, websocket => webSockets.emit('connection', websocket, request))
  } catch { socket.destroy() }
})
webSockets.on('connection', socket => socket.send(JSON.stringify(snapshot())))
collector.setPublisher(value => {
  const message = JSON.stringify(value)
  for (const socket of webSockets.clients) if (socket.readyState === 1) socket.send(message)
})

udp = dgram.createSocket('udp4')
udp.on('message', collector.handleDatagram)
udp.on('listening', () => { serviceState.udpReady = true })
udp.on('close', () => { serviceState.udpReady = false })
udp.bind(udpPort, udpBind)
collector.start()
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
  collector.stop()
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
