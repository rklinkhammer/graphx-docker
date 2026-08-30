import dgram from 'node:dgram'
import { createReadStream, existsSync, readFileSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join, normalize } from 'node:path'
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
const nodes = Object.fromEntries(topology.nodes.map(node => [node.id, { status: 'starting', lastSeen: null }]))
const edges = Object.fromEntries(topology.edges.map(edge => [edge.id, {
  messages: 0, received: 0, wireBytes: 0, drops: 0, errors: 0, rate: 0,
  latencyUs: 0, reconnects: 0, backpressureEvents: 0, backpressureUs: 0,
  rejected: 0, connection: 'disconnected', lastSequence: 0, lastSeen: null,
  windowCount: 0, windowStart: Date.now(),
}]))
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png' }

function snapshot() {
  return { kind: 'snapshot', graph: graph.id, topology,
    telemetry: { websocket: websocketPath, heartbeatTimeoutMs: heartbeatTimeout }, state, nodes, edges,
    recent: recent.slice(0, 30), timestamp: Date.now() }
}

function json(response, status, value) {
  response.writeHead(status, { 'content-type': 'application/json', 'access-control-allow-origin': '*' })
  response.end(JSON.stringify(value))
}

function prometheus() {
  const lines = ['# HELP graphx_edge_messages_total GraphX edge events.',
    '# TYPE graphx_edge_messages_total counter']
  for (const [id, edge] of Object.entries(edges)) {
    const label = `edge="${id}"`
    lines.push(`graphx_edge_messages_total{${label},direction="sent"} ${edge.messages}`)
    lines.push(`graphx_edge_messages_total{${label},direction="received"} ${edge.received}`)
    lines.push(`graphx_edge_wire_bytes_total{${label}} ${edge.wireBytes}`)
    lines.push(`graphx_edge_errors_total{${label}} ${edge.errors}`)
    lines.push(`graphx_edge_reconnects_total{${label}} ${edge.reconnects}`)
    lines.push(`graphx_edge_backpressure_events_total{${label}} ${edge.backpressureEvents}`)
    lines.push(`graphx_edge_backpressure_seconds_total{${label}} ${edge.backpressureUs / 1e6}`)
    lines.push(`graphx_edge_latency_seconds{${label}} ${edge.latencyUs / 1e6}`)
    lines.push(`graphx_edge_connected{${label}} ${edge.connection === 'connected' ? 1 : 0}`)
  }
  return `${lines.join('\n')}\n`
}

const server = createServer((request, response) => {
  const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`)
  if (url.pathname === '/api/health') return json(response, 200, { status: 'ok', service: 'graphx-telemetry' })
  if (url.pathname === '/api/topology') return json(response, 200, snapshot())
  if (url.pathname === '/metrics') {
    response.writeHead(200, { 'content-type': 'text/plain; version=0.0.4; charset=utf-8' })
    return response.end(prometheus())
  }
  if (url.pathname.startsWith('/api/control/') && request.method === 'POST') {
    const action = url.pathname.split('/').pop()
    if (action === 'reset') {
      state = { paused: false, fault: false, updatedAt: new Date().toISOString() }
      recent.length = 0
      for (const edge of Object.values(edges)) {
        const connection = edge.connection
        Object.assign(edge, {
          messages: 0, received: 0, wireBytes: 0, drops: 0, errors: 0, rate: 0,
          latencyUs: 0, reconnects: 0, backpressureEvents: 0, backpressureUs: 0,
          rejected: 0, connection, lastSequence: 0,
          windowCount: 0, windowStart: Date.now(),
        })
      }
      state.updatedAt = new Date().toISOString()
      broadcast()
      return json(response, 200, { accepted: true, action, state })
    }
    return json(response, 501, { accepted: false, action,
      error: 'runtime control channel is not implemented; use the documented netem hooks' })
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
udp.on('message', data => {
  try {
    const event = JSON.parse(data.toString('utf8'))
    const receivedAt = Date.now()
    if (nodes[event.nodeId]) nodes[event.nodeId] = { status: 'running', lastSeen: receivedAt }
    const edge = edges[event.edgeId]
    if (edge) {
      edge.lastSeen = receivedAt
      edge.lastSequence = event.sequence || edge.lastSequence
      if (event.event === 'send') {
        // A live data event is authoritative evidence that the transport path is
        // usable, including after the telemetry service itself has restarted.
        edge.connection = 'connected'
        edge.messages += 1
        edge.wireBytes += event.wireBytes || 0
        edge.windowCount += 1
        const elapsed = Math.max(Date.now() - edge.windowStart, 1)
        edge.rate = Math.round(edge.windowCount * 10000 / elapsed) / 10
        if (elapsed >= 5000) { edge.windowCount = 0; edge.windowStart = Date.now() }
      }
      if (event.event === 'receive') {
        edge.connection = 'connected'
        edge.received += 1
        edge.latencyUs = event.latencyUs || 0
        recent.unshift(event)
        if (recent.length > 100) recent.length = 100
      }
      if (event.event === 'error') { edge.errors += 1; edge.connection = 'error' }
      if (event.event === 'connection') edge.connection = event.message || 'unknown'
      if (event.event === 'reconnect') edge.reconnects += 1
      if (event.event === 'backpressure') {
        edge.backpressureEvents += 1
        edge.backpressureUs += event.latencyUs || 0
        if (event.message === 'rejected') edge.rejected += 1
      }
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
