import { constants, openSync, fstatSync, readFileSync, closeSync, lstatSync } from 'node:fs'
import { join } from 'node:path'
import { createConnection } from 'node:net'
import { randomBytes } from 'node:crypto'

const LIMIT = 65536
const fail = (status, message) => Object.assign(new Error(message), { status })
// No paths or host command arguments are accepted from callers.
export class NodeConsole {
  constructor({ directory, graph, nodes, now = Date.now, connect = createConnection, audit = () => {}, validWriter = () => false, endpointStat = lstatSync }) {
    Object.assign(this, { directory, graph, now, connect, audit, validWriter, endpointStat })
    this.nodes = new Map(nodes.map(node => [node.node_id, node.execution.kind]))
    this.sessions = new Map()
    this.directoryIdentity = null
    this.timer = setInterval(() => this.sweep(), 1000).unref()
  }
  read(node) {
    if (!this.nodes.has(node) || !/^[a-z][a-z0-9_-]{0,63}$/.test(node)) throw fail(404, 'unknown node')
    if (!this.directory) throw fail(503, 'node logs unavailable for this deployment')
    const dir = lstatSync(this.directory)
    if (!dir.isDirectory() || dir.isSymbolicLink()) throw fail(503, 'console directory identity changed')
    const identity = `${dir.dev}:${dir.ino}`
    if (this.directoryIdentity && identity !== this.directoryIdentity) throw fail(503, 'console directory identity changed')
    this.directoryIdentity = identity
    const fd = openSync(join(this.directory, `${node}.json`), constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK)
    let value
    try {
      const stat = fstatSync(fd)
      if (!stat.isFile() || stat.uid !== dir.uid || stat.nlink !== 1 || stat.size > 4 * LIMIT + 4096 || stat.mode & 0o222)
        throw fail(503, 'invalid console snapshot')
      value = JSON.parse(readFileSync(fd, 'utf8'))
    } finally { closeSync(fd) }
    if (value.version !== 1 || value.graph !== this.graph || value.node !== node ||
        !/^console-[a-f0-9]{32}$/.test(value.generation) ||
        !/^(?:[a-f0-9]{2})*$/.test(value.hex) || value.hex.length > LIMIT * 2 ||
        !['running', 'stopped', 'unavailable'].includes(value.status) ||
        (value.status === 'running' && (typeof value.runtime !== 'string' || value.runtime.length > 256)) ||
        (value.diagnostics_hex != null && (typeof value.diagnostics_hex !== 'string' ||
          value.diagnostics_hex.length > LIMIT * 2 || !/^(?:[a-f0-9]{2})*$/.test(value.diagnostics_hex))) ||
        !Number.isFinite(value.updated) || value.updated * 1000 > this.now() + 5000)
      throw fail(503, 'invalid console snapshot')
    return { ...value, stale: this.now() - value.updated * 1000 > 15000 }
  }
  serial(node) {
    if (this.nodes.get(node) !== 'qemu') throw fail(400, 'serial access requires a QEMU node')
    const log = this.read(node)
    if (log.stale || log.status !== 'running') throw fail(503, 'guest is not available')
    const generation = `${log.generation}:${log.runtime}`
    let session = this.sessions.get(node)
    if (session && session.generation !== generation) { this.drop(node); session = null }
    if (!session) {
      if (this.sessions.size >= 64) throw fail(429, 'serial connection capacity reached')
      const path = join(this.directory, `${node}.sock`)
      const stat = this.endpointStat(path)
      if (!stat.isSocket() || stat.uid !== 65532 || `${stat.dev}:${stat.ino}` !== log.serial_identity) throw fail(503, 'serial endpoint unavailable')
      const socket = this.connect({ path })
      session = { socket, generation, bytes: Buffer.alloc(0), end: 0, connected: false, last: this.now(), writer: null }
      this.sessions.set(node, session)
      socket.on('connect', () => { session.connected = true; this.audit({ node, event: 'serial-connected' }) })
      socket.on('data', bytes => {
        session.end += bytes.length
        session.bytes = Buffer.concat([session.bytes, bytes]).subarray(-LIMIT)
      })
      socket.on('error', () => this.drop(node))
      socket.on('close', () => { if (this.sessions.get(node) === session) this.drop(node) })
    }
    session.last = this.now()
    return session
  }
  release(node, session, reason) {
    if (session.writer) this.audit({ node, actor: session.writer.actor, event: 'serial-writer-released', reason })
    session.writer = null
  }
  drop(node) {
    const session = this.sessions.get(node)
    if (!session) return
    this.sessions.delete(node)
    this.release(node, session, 'disconnected')
    session.socket.destroy()
    this.audit({ node, event: 'serial-disconnected' })
  }
  sweep() {
    for (const [node, session] of this.sessions) {
      if (session.last + 30000 < this.now()) { this.drop(node); continue }
      if (session.writer && (session.writer.expires < this.now() || session.writer.deadline < this.now() ||
          !this.validWriter(session.writer.credential, node))) this.release(node, session, 'expired or revoked')
      try {
        const value = this.read(node)
        const endpoint = this.endpointStat(join(this.directory, `${node}.sock`))
        if (!endpoint.isSocket() || endpoint.uid !== 65532 || `${endpoint.dev}:${endpoint.ino}` !== value.serial_identity) { this.drop(node); continue }
        if (value.stale || value.status !== 'running' || `${value.generation}:${value.runtime}` !== session.generation) this.drop(node)
      } catch { this.drop(node) }
    }
  }
  view(node) {
    const s = this.serial(node)
    return { generation: s.generation, status: s.connected ? 'connected' : 'connecting',
      hex: s.bytes.toString('hex'), start: s.end - s.bytes.length, end: s.end,
      writer: s.writer ? { actor: s.writer.actor, expires: s.writer.expires } : null }
  }
  command(node, body, principal, credential, sessionExpires = Infinity) {
    if (!body || Array.isArray(body) || Object.keys(body).some(key => !['action', 'generation', 'lease', 'hex'].includes(key)))
      throw fail(400, 'invalid serial request')
    if (!['acquire', 'renew', 'input', 'release'].includes(body.action)) throw fail(400, 'unknown serial operation')
    this.sweep()
    const s = this.serial(node)
    if (body.generation !== s.generation) throw fail(409, 'guest generation changed')
    if (!s.connected) throw fail(503, 'serial connection is not ready')
    if (body.action === 'acquire') {
      if (s.writer) throw fail(409, 'serial writer already held')
      const lease = randomBytes(24).toString('hex')
      s.writer = { lease, actor: principal.id, credential, expires: this.now() + 10000, deadline: Math.min(this.now() + 900000, sessionExpires) }
      this.audit({ node, actor: principal.id, event: 'serial-writer-acquired' })
      return { lease, generation: s.generation, expires: s.writer.expires }
    }
    if (!s.writer || s.writer.actor !== principal.id || body.lease !== s.writer.lease || credential !== s.writer.credential)
      throw fail(403, 'serial writer lease required')
    if (body.action === 'release') { this.release(node, s, 'released'); return { released: true } }
    if (body.action === 'input') {
      if (typeof body.hex !== 'string' || !/^(?:[a-f0-9]{2}){1,4096}$/.test(body.hex)) throw fail(400, 'invalid serial input')
      if (s.socket.writableLength > 8192) throw fail(429, 'serial input backpressure')
      s.socket.write(Buffer.from(body.hex, 'hex'))
    }
    s.writer.expires = this.now() + 10000
    return { accepted: true, expires: s.writer.expires }
  }
  close() { clearInterval(this.timer); for (const node of this.sessions.keys()) this.drop(node) }
}

export async function nodeConsoleRequest(context, console_, request, response, url) {
  const control = url.pathname.startsWith('/api/control/serial/')
  const node = control ? url.pathname.slice('/api/control/serial/'.length) : url.pathname.split('/')[3]
  const { json } = context
  response.setTimeout?.(5000, () => response.destroy())
  try {
    if (url.search) throw fail(400, 'query parameters are not supported')
    if (!control) {
      if (request.method !== 'GET') throw fail(405, 'GET required')
      if (!context.observationToken || !context.authorized(request, context.observationToken)) throw fail(401, 'observation credential required')
      const value = url.pathname.endsWith('/serial') ? console_.view(node) : console_.read(node)
      return json(response, 200, value)
    }
    if (request.method !== 'POST') throw fail(405, 'POST required')
    if (!context.requestOriginAllowed(request)) throw fail(403, 'origin not allowed')
    const principal = context.controlPrincipal(request)
    if (!principal) throw fail(401, 'control credential required')
    if (!context.serialPermitted(principal, node)) throw fail(403, 'node serial permission required')
    if (!context.withinRateLimit(request, 240, 60000, 'control')) throw fail(429, 'serial rate limit exceeded')
    if (request.headers['content-type'] !== 'application/json') throw fail(400, 'JSON required')
    let bytes = 0; const parts = []
    const timeout = setTimeout(() => request.destroy(), 10000).unref()
    let body
    try {
      for await (const chunk of request) { bytes += chunk.length; if (bytes > 10000) throw fail(413, 'serial input too large'); parts.push(chunk) }
      try { body = JSON.parse(Buffer.concat(parts).toString('utf8')) }
      catch { throw fail(400, 'invalid JSON') }
    } finally { clearTimeout(timeout) }
    return json(response, 200, console_.command(node, body, principal, request.headers.authorization, request.consoleExpires))
  } catch (error) {
    if (control) context.controlPlane.deny({ action: 'serial', targets: [node], reason: 'serial request rejected' })
    return json(response, error.status || 503, { error: error.status ? error.message : 'node console unavailable' })
  }
}
