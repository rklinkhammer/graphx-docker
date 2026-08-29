import dgram from 'node:dgram'
import { createReadStream, existsSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join, normalize } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocketServer } from 'ws'

const root = normalize(process.env.GRAPHX_WEB_ROOT || join(fileURLToPath(new URL('.', import.meta.url)), '../../web/dist'))
const port = Number(process.env.PORT || 8080)
const udpPort = Number(process.env.GRAPHX_TELEMETRY_PORT || 9000)
let state = { paused: false, fault: false, updatedAt: new Date().toISOString() }
const recent = []
const nodes = Object.fromEntries(['generator', 'transform', 'sink'].map(id => [id, { status: 'starting', lastSeen: null }]))
const edges = Object.fromEntries(['samples', 'transformed'].map(id => [id, {
  messages: 0, received: 0, wireBytes: 0, drops: 0, errors: 0, rate: 0,
  latencyUs: 0, reconnects: 0, backpressureEvents: 0, backpressureUs: 0,
  rejected: 0, connection: 'disconnected', lastSequence: 0, lastSeen: null,
  windowCount: 0, windowStart: Date.now(),
}]))
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png' }

function snapshot() {
  return { kind: 'snapshot', graph: 'sample-pipeline', state, nodes, edges,
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
      for (const edge of Object.values(edges)) Object.assign(edge, {
        messages: 0, received: 0, wireBytes: 0, drops: 0, errors: 0, rate: 0,
        latencyUs: 0, reconnects: 0, backpressureEvents: 0, backpressureUs: 0,
        rejected: 0, connection: 'disconnected', lastSequence: 0,
        windowCount: 0, windowStart: Date.now(),
      })
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

const webSockets = new WebSocketServer({ server, path: '/ws' })
webSockets.on('connection', socket => socket.send(JSON.stringify(snapshot())))
function broadcast() {
  const message = JSON.stringify(snapshot())
  for (const socket of webSockets.clients) if (socket.readyState === 1) socket.send(message)
}

const udp = dgram.createSocket('udp4')
udp.on('message', data => {
  try {
    const event = JSON.parse(data.toString('utf8'))
    if (nodes[event.nodeId]) nodes[event.nodeId] = { status: 'running', lastSeen: event.timestamp }
    const edge = edges[event.edgeId]
    if (edge) {
      edge.lastSeen = event.timestamp
      edge.lastSequence = event.sequence || edge.lastSequence
      if (event.event === 'send') {
        edge.messages += 1
        edge.wireBytes += event.wireBytes || 0
        edge.windowCount += 1
        const elapsed = Math.max(Date.now() - edge.windowStart, 1)
        edge.rate = Math.round(edge.windowCount * 10000 / elapsed) / 10
        if (elapsed >= 5000) { edge.windowCount = 0; edge.windowStart = Date.now() }
      }
      if (event.event === 'receive') {
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
server.listen(port, '0.0.0.0', () => console.log(`GraphX telemetry HTTP/WebSocket :${port}, UDP :${udpPort}`))
