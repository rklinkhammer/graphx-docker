import { createHash, randomBytes } from 'node:crypto'

const opaque = () => randomBytes(32).toString('base64url')
const text = value => typeof value === 'string' && value.length > 0 && value.length <= 8192

// Tokens stay in bounded server memory. Every use revalidates the underlying
// credentials, so rotation, invalid policy, and process replacement revoke access.
export function createConsoleSessions({ graph, observe, control, originAllowed, json,
  secure = false, now = Date.now, capacity = 128, handoffMs = 60_000, sessionMs = 8 * 60 * 60_000 }) {
  const handoffs = new Map(), sessions = new Map()
  const cookieName = `graphx_session_${createHash('sha256').update(graph).digest('hex').slice(0, 16)}`
  const valid = entry => entry && entry.expires > now() && observe(entry.observation) &&
    (!entry.control || control(entry.control))
  function prune() {
    for (const store of [handoffs, sessions])
      for (const [key, entry] of store) if (!valid(entry)) store.delete(key)
  }
  function get(request) {
    prune()
    const cookies = (request.headers.cookie || '').split(';').map(value => value.trim())
    const matches = cookies.filter(value => value.startsWith(cookieName + '='))
    if (matches.length !== 1) return null
    const entry = sessions.get(matches[0].slice(cookieName.length + 1))
    if (!entry || (request.headers.origin && request.headers.origin !== entry.origin) ||
        request.headers['sec-fetch-site'] === 'cross-site') return null
    return entry
  }
  function mint(observation, operator, origin) {
    prune()
    if (!text(observation) || !observe(observation) ||
        (operator != null && (!text(operator) || !control(operator))))
      throw new Error('invalid console credentials')
    if (handoffs.size >= capacity) throw new Error('console handoff capacity exceeded')
    const code = opaque()
    handoffs.set(code, { observation, control: operator || null, origin, expires: now() + handoffMs })
    return code
  }
  function exchange(code, origin) {
    prune()
    const entry = handoffs.get(code)
    if (!entry || entry.origin !== origin) throw new Error('console login expired or invalid; reopen with graphx example open')
    handoffs.delete(code)
    if (sessions.size >= capacity) throw new Error('console session capacity exceeded')
    const id = opaque(), session = { ...entry, expires: now() + sessionMs, csrf: opaque() }
    sessions.set(id, session)
    return { session, cookie: `${cookieName}=${id}; Path=/; HttpOnly; SameSite=Strict; Max-Age=${Math.floor(sessionMs / 1000)}${secure ? '; Secure' : ''}` }
  }
  function attach(request, websocket = false) {
    if (request.headers.authorization) return false // Explicit bearer authentication takes precedence.
    const entry = get(request)
    if (!entry) return false
    if (websocket && request.headers.origin !== entry.origin) return false
    const controlling = request.url.startsWith('/api/control/')
    if (controlling && (request.headers['x-graphx-csrf'] !== entry.csrf ||
        (request.method !== 'GET' && request.headers.origin !== entry.origin))) return false
    const token = controlling ? entry.control : entry.observation
    if (!token) return false
    request.consoleExpires = entry.expires
    request.consoleSession = true
    request.headers.authorization = `Bearer ${token}`
    return true
  }
  function status(entry) {
    return { authenticated: true, graph, control: Boolean(entry.control), csrf: entry.csrf, expires: entry.expires }
  }
  async function handle(request, response, path) {
    if (path === '/api/console/session' && request.method === 'GET') {
      const entry = get(request)
      return json(response, entry ? 200 : 401, entry ? status(entry) : { authenticated: false })
    }
    if (request.method !== 'POST') return json(response, 405, { error: 'method not allowed' })
    if (!request.headers.origin || !originAllowed(request) ||
        request.headers['content-type']?.split(';')[0] !== 'application/json')
      return json(response, 403, { error: 'console login requires an allowed origin and JSON' })
    let size = 0, chunks = []
    const timer = setTimeout(() => request.destroy(), 10_000).unref()
    try {
      for await (const chunk of request) {
        size += chunk.length
        if (size > 16_384) return json(response, 413, { error: 'console request too large' })
        chunks.push(chunk)
      }
      const body = JSON.parse(Buffer.concat(chunks).toString('utf8'))
      if (!body || typeof body !== 'object' || Array.isArray(body)) throw new Error('invalid console request')
      if (path === '/api/console/handoff') {
        if (Object.keys(body).some(key => key !== 'control_token')) throw new Error('invalid console request')
        const bearer = request.headers.authorization || ''
        if (!bearer.startsWith('Bearer ')) throw new Error('invalid console credentials')
        const code = mint(bearer.slice(7), body.control_token, request.headers.origin)
        return json(response, 201, { code, expires_in: handoffMs / 1000 })
      }
      if (Object.keys(body).length !== 1 || !text(body.code)) throw new Error('invalid console request')
      const result = exchange(body.code, request.headers.origin)
      return json(response, 200, status(result.session), { 'set-cookie': result.cookie })
    } catch { return json(response, 401, { error: 'console login failed; reopen with graphx example open' }) }
    finally { clearTimeout(timer); chunks = [] }
  }
  return { get, mint, exchange, attach, handle }
}
