import dgram from 'node:dgram'
import { timingSafeEqual } from 'node:crypto'
import { closeSync, createReadStream, existsSync, openSync, readFileSync, readSync, readdirSync, statSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocketServer } from 'ws'
import { parse } from 'yaml'

const root = normalize(process.env.GRAPHX_WEB_ROOT || join(fileURLToPath(new URL('.', import.meta.url)), '../../web/dist'))
const port = Number(process.env.PORT || 8080)
const udpPort = Number(process.env.GRAPHX_TELEMETRY_PORT || 9000)
const configPath = process.env.GRAPHX_CONFIG || normalize(join(fileURLToPath(new URL('.', import.meta.url)), '../../graphx.yaml'))
const config = parse(readFileSync(configPath, 'utf8'))
const graph = config.graph || { id: 'graphx', nodes: [], edges: [] }
const deployment = config.deployment?.services || {}
const transport = config.transport || {}
const heartbeatTimeout = Number(process.env.GRAPHX_HEARTBEAT_TIMEOUT_MS || config.observability?.telemetry?.heartbeat_timeout_ms || 5000)
const websocketPath = config.observability?.telemetry?.websocket || '/ws'
const configuredCapture = config.observability?.capture || { enabled: false, provider: '' }
const captureConfig = { ...configuredCapture,
  enabled: process.env.GRAPHX_CAPTURE_ENABLED == null ? Boolean(configuredCapture.enabled) :
    ['1', 'true', 'yes', 'on'].includes(process.env.GRAPHX_CAPTURE_ENABLED.toLowerCase()),
  provider: process.env.GRAPHX_CAPTURE_PROVIDER || configuredCapture.provider || '',
}
const captureDirectory = resolve(process.env.GRAPHX_CAPTURE_DIR || captureConfig.directory || 'captures')
const controlToken = process.env.GRAPHX_CONTROL_TOKEN || ''
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
  return { kind: 'snapshot', graph: graph.id, topology,
    telemetry: { websocket: websocketPath, heartbeatTimeoutMs: heartbeatTimeout,
      rateWindowSeconds, latencyBoundsUs }, state,
    control: { available: controlToken.length > 0, connectedNodes: liveEndpoints.length,
      acknowledgements: Object.fromEntries(controlAcks) }, nodes, edges: edgeViews,
    capture: { enabled: Boolean(captureConfig.enabled), provider: captureConfig.provider || null,
      format: files.length ? 'pcapng' : null,
      files }, recent: recentWithCapture(), timestamp }
}

function json(response, status, value) {
  response.writeHead(status, { 'content-type': 'application/json', 'access-control-allow-origin': '*' })
  response.end(JSON.stringify(value))
}

function authorized(request) {
  if (!controlToken) return false
  const supplied = request.headers.authorization?.startsWith('Bearer ')
    ? request.headers.authorization.slice(7) : ''
  const expectedBytes = Buffer.from(controlToken)
  const suppliedBytes = Buffer.from(supplied)
  return expectedBytes.length === suppliedBytes.length &&
    timingSafeEqual(expectedBytes, suppliedBytes)
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
  ]
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

const server = createServer((request, response) => {
  const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`)
  if (url.pathname === '/api/health') return json(response, 200, { status: 'ok', service: 'graphx-telemetry' })
  if (url.pathname === '/api/topology') return json(response, 200, snapshot())
  if (url.pathname === '/api/captures') return json(response, 200, snapshot().capture)
  if (url.pathname === '/metrics') {
    response.writeHead(200, { 'content-type': 'text/plain; version=0.0.4; charset=utf-8' })
    return response.end(prometheus())
  }
  if (url.pathname.startsWith('/api/control/') && request.method === 'POST') {
    const action = url.pathname.split('/').pop()
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
    if (!controlToken)
      return json(response, 503, { accepted: false, action,
        error: 'runtime control is disabled; set GRAPHX_CONTROL_TOKEN' })
    if (!authorized(request))
      return json(response, 401, { accepted: false, action, error: 'invalid control token' })
    const endpoints = liveControlEndpoints()
    if (!endpoints.length)
      return json(response, 409, { accepted: false, action, error: 'no live runtime nodes' })
    const command = Buffer.from(JSON.stringify({ kind: 'control', action, timestamp: Date.now() }))
    for (const [, endpoint] of endpoints) udp.send(command, endpoint.port, endpoint.address)
    state = { ...state, paused: action === 'pause', updatedAt: new Date().toISOString() }
    broadcast()
    return json(response, 202, { accepted: true, action, delivered: endpoints.length, state })
  }
  if (url.pathname.startsWith('/captures/')) {
    let name
    try { name = decodeURIComponent(url.pathname.slice('/captures/'.length)) }
    catch { return json(response, 400, { error: 'invalid capture name' }) }
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}\.pcapng$/.test(name))
      return json(response, 404, { error: 'capture not found' })
    const available = captureFiles().some(file => file.name === name)
    const capturePath = join(captureDirectory, name)
    if (!available || !existsSync(capturePath)) return json(response, 404, { error: 'capture not found' })
    response.writeHead(200, { 'content-type': 'application/vnd.tcpdump.pcap',
      'content-disposition': `attachment; filename="${name}"` })
    return createReadStream(capturePath).pipe(response)
  }
  let requested = url.pathname === '/' ? 'index.html' : url.pathname.slice(1)
  const file = normalize(join(root, requested))
  if (!file.startsWith(root) || !existsSync(file)) requested = 'index.html'
  const fallback = normalize(join(root, requested))
  if (!existsSync(fallback)) return json(response, 404, { error: 'web assets not built' })
  response.writeHead(200, { 'content-type': types[extname(fallback)] || 'application/octet-stream' })
  createReadStream(fallback).pipe(response)
})

const webSockets = new WebSocketServer({ server, path: websocketPath })
webSockets.on('connection', socket => socket.send(JSON.stringify(snapshot())))
function broadcast() {
  const message = JSON.stringify(snapshot())
  for (const socket of webSockets.clients) if (socket.readyState === 1) socket.send(message)
}

const udp = dgram.createSocket('udp4')
udp.on('message', (data, remote) => {
  try {
    const event = JSON.parse(data.toString('utf8'))
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

udp.bind(udpPort, '0.0.0.0')
setInterval(() => {
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
server.listen(port, '0.0.0.0', () => console.log(`GraphX telemetry HTTP/WebSocket :${port}, UDP :${udpPort}`))
