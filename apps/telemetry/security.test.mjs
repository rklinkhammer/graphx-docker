import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:net'
import net from 'node:net'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'
import { RateLimiter, ReplayCache, parseRequestUrl, readSecret, sanitizeControlAcknowledgement,
  sanitizeTelemetryEvent, signEnvelope, tokenMatches, validateTelemetryEvent, verifyEnvelope,
  webSocketBearer } from './security.mjs'

const secret = '0123456789abcdef0123456789abcdef'

test('HMAC envelope authenticates once and rejects tampering, replay, and stale timestamps', () => {
  const now = 1_800_000_000_000
  const envelope = signEnvelope({ kind: 'trace', nodeId: 'node' }, secret, now,
    '00112233445566778899aabbccddeeff')
  const cache = new ReplayCache()
  assert.deepEqual(verifyEnvelope(envelope, secret, cache, now), envelope.payload)
  assert.equal(verifyEnvelope(envelope, secret, cache, now), null)
  assert.equal(verifyEnvelope({ ...envelope, payload: { kind: 'trace', nodeId: 'other' } },
    secret, new ReplayCache(), now), null)
  assert.equal(verifyEnvelope(signEnvelope(envelope.payload, secret, now - 30001),
    secret, new ReplayCache(), now), null)
})

test('secrets and bearer tokens enforce minimum length and constant-time comparison shape', () => {
  assert.equal(readSecret('TOKEN', { TOKEN: secret }), secret)
  assert.throws(() => readSecret('TOKEN', { TOKEN: 'short' }), /at least 32/)
  assert.throws(() => readSecret('TOKEN', { TOKEN: secret, TOKEN_FILE: '/unused' }), /mutually/)
  assert.equal(tokenMatches(secret, `Bearer ${secret}`), true)
  assert.equal(tokenMatches(secret, 'Bearer wrong'), false)
})

test('telemetry validation rejects unknown identities and unbounded values', () => {
  const nodes = new Set(['generator'])
  const edges = new Set(['samples'])
  const valid = { kind: 'trace', event: 'send', nodeId: 'generator', edgeId: 'samples',
    timestamp: Date.now(), wireBytes: 64 }
  assert.equal(validateTelemetryEvent(valid, nodes, edges), true)
  assert.equal(validateTelemetryEvent({ ...valid, nodeId: 'attacker' }, nodes, edges), false)
  assert.equal(validateTelemetryEvent({ ...valid, wireBytes: 1e12 }, nodes, edges), false)
  assert.equal(validateTelemetryEvent({ ...valid, message: 'x'.repeat(257) }, nodes, edges), false)
  assert.equal(validateTelemetryEvent({ ...valid, event: 'connection', message: 'arbitrary' },
    nodes, edges), false)
  assert.equal(validateTelemetryEvent({ ...valid, event: 'connection', message: 'connected' },
    nodes, edges), true)
  assert.equal(validateTelemetryEvent({ ...valid, event: 'backpressure', message: 'blocked' },
    nodes, edges), true)
})

test('control acknowledgements require correlated bounded command identity and state', () => {
  const nodes = new Set(['generator'])
  const edges = new Set(['samples'])
  const valid = { kind: 'control_ack', nodeId: 'generator', action: 'pause', accepted: true,
    commandId: '8b85ab27-9318-4c01-aa2d-6ad93ca7f84b', state: 'paused' }
  assert.equal(validateTelemetryEvent(valid, nodes, edges), true)
  assert.equal(validateTelemetryEvent({ ...valid, commandId: 'forged' }, nodes, edges), false)
  assert.equal(validateTelemetryEvent({ ...valid, state: 'unknown' }, nodes, edges), false)
  assert.equal(validateTelemetryEvent({ ...valid, nodeId: 'attacker' }, nodes, edges), false)
})

test('control acknowledgement sanitization retains only protocol error codes', () => {
  const acknowledgement = { kind: 'control_ack', accepted: false, error: 'busy' }
  assert.deepEqual(sanitizeControlAcknowledgement(acknowledgement), acknowledgement)
  assert.equal(sanitizeControlAcknowledgement({ ...acknowledgement,
    error: secret }).error, 'runtime-rejected')
  assert.equal(sanitizeControlAcknowledgement({ ...acknowledgement,
    error: `failure:${secret}` }).error, 'runtime-rejected')
  assert.equal(sanitizeControlAcknowledgement({ ...acknowledgement,
    accepted: true, error: secret }).error, undefined)
  assert.equal(sanitizeControlAcknowledgement({ kind: 'trace', message: secret }).message, secret)
})

test('telemetry sanitization removes credentials and unknown fields before fan-out', () => {
  const credential = 'runtime-credential-01234567890123456789'
  const event = sanitizeTelemetryEvent({ kind: 'trace', event: 'error', nodeId: 'generator',
    edgeId: 'samples', message: `failure:${credential}`, messageId: `id:${credential}`,
    parentMessageId: credential, traceId: credential, type: `type:${credential}`,
    attackerControlled: credential }, [credential])
  assert.equal(JSON.stringify(event).includes(credential), false)
  assert.equal(event.message, 'failure:[credential-redacted]')
  assert.equal('attackerControlled' in event, false)
  const acknowledgement = sanitizeTelemetryEvent(sanitizeControlAcknowledgement({
    kind: 'control_ack', nodeId: 'generator', action: 'pause', accepted: false,
    commandId: '8b85ab27-9318-4c01-aa2d-6ad93ca7f84b', error: credential,
    message: credential,
  }), [credential])
  assert.equal(acknowledgement.error, 'runtime-rejected')
  assert.equal('message' in acknowledgement, false)
})

test('browser WebSocket bearer subprotocol is decoded', () => {
  const encoded = Buffer.from(secret).toString('base64url')
  assert.equal(webSocketBearer([`graphx-auth.${encoded}`], secret), secret)
  assert.equal(webSocketBearer([], ''), null)
})

test('rate limiter enforces capacity, isolation, expiry, and request counts', () => {
  const limiter = new RateLimiter(3)
  assert.equal(limiter.allow('first', 2, 1000, 100), true)
  assert.equal(limiter.allow('first', 2, 1000, 101), true)
  assert.equal(limiter.allow('first', 2, 1000, 102), false)
  assert.equal(limiter.allow('second', 2, 1000, 103), true)
  assert.equal(limiter.allow('third', 2, 1000, 104), true)
  assert.equal(limiter.allow('fourth', 2, 1000, 105), true)
  assert.equal(limiter.size, 3)
  assert.equal(limiter.has('first'), false)
  assert.equal(limiter.allow('fresh', 1, 1000, 2000), true)
  assert.equal(limiter.size, 1)
})

test('request URL parsing is total for malformed untrusted targets', () => {
  assert.equal(parseRequestUrl('/api/health').pathname, '/api/health')
  assert.equal(parseRequestUrl('http://['), null)
  assert.equal(parseRequestUrl('//['), null)
})

test('secret files accept one trailing newline and remain bounded', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-security-test-'))
  try {
    const path = join(directory, 'token')
    await writeFile(path, `${secret}\n`, { mode: 0o600 })
    assert.equal(readSecret('TOKEN', { TOKEN_FILE: path }), secret)
  } finally { await rm(directory, { recursive: true }) }
})

async function availablePort() {
  const server = createServer()
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const { port } = server.address()
  server.close()
  await once(server, 'close')
  return port
}

function rawRequest(port, target, upgrade = false) {
  return new Promise((resolveRequest, reject) => {
    const socket = net.connect(port, '127.0.0.1', () => socket.end(
      `GET ${target} HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\n` +
      (upgrade ? 'Connection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n' : 'Connection: close\r\n') + '\r\n'))
    let response = ''
    socket.on('data', data => { response += data })
    socket.on('error', reject)
    socket.on('close', () => resolveRequest(response))
  })
}

test('telemetry survives malformed HTTP and WebSocket request targets', { timeout: 10000 }, async () => {
  const directory = dirname(fileURLToPath(import.meta.url))
  const port = await availablePort()
  const udpPort = await availablePort()
  const child = spawn(process.execPath, ['server.mjs'], {
    cwd: directory,
    env: { ...process.env, PORT: String(port), GRAPHX_TELEMETRY_PORT: String(udpPort),
      GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
      GRAPHX_CONFIG: resolve(directory, '../../graphx.yaml'), GRAPHX_OBSERVATION_TOKEN: secret },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  try {
    for (let attempt = 0; attempt < 50; ++attempt) {
      try { if ((await fetch(`http://127.0.0.1:${port}/api/health`)).ok) break }
      catch { /* The child is still starting. */ }
      await new Promise(resolveWait => setTimeout(resolveWait, 50))
    }
    const invalid = await rawRequest(port, 'http://[')
    assert.match(invalid, /^HTTP\/1\.1 400 /)
    await rawRequest(port, 'http://[', true)
    const health = await fetch(`http://127.0.0.1:${port}/api/health`)
    assert.equal(health.status, 200)
    assert.equal((await fetch(`http://127.0.0.1:${port}/api/live`)).status, 200)
    assert.equal((await fetch(`http://127.0.0.1:${port}/api/ready`)).status, 200)
    assert.equal((await fetch(`http://127.0.0.1:${port}/api/graph/ready`)).status, 401)
    const headers = { authorization: `Bearer ${secret}` }
    assert.equal((await fetch(`http://127.0.0.1:${port}/api/graph/ready`, { headers })).status, 503)
    assert.equal((await fetch(`http://127.0.0.1:${port}/api/slo`)).status, 401)
    assert.equal((await fetch(`http://127.0.0.1:${port}/api/slo`, { headers })).status, 200)
    const metrics = await (await fetch(`http://127.0.0.1:${port}/metrics`, { headers })).text()
    assert.match(metrics, /graphx_service_ready 1/)
    assert.match(metrics, /graphx_slo_status\{status="warming"\} 1/)
    assert.match(metrics, /graphx_slo_ratio/)
    assert.match(metrics, /graphx_otlp_exports_total\{outcome="retried"\} 0/)
    assert.match(metrics, /graphx_otlp_queue_bytes 0/)
    assert.equal(child.exitCode, null)
  } finally {
    child.kill('SIGTERM')
    if (child.exitCode == null) await once(child, 'exit')
  }
})
