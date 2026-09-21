import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createSocket } from 'node:dgram'
import { createServer } from 'node:net'
import { dirname, resolve } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'
import { normalizedConfigEnvironment } from './test-config.mjs'
import { signEnvelope } from './security.mjs'

const here = dirname(fileURLToPath(import.meta.url))
async function port() {
  const server = createServer().listen(0, '127.0.0.1')
  await once(server, 'listening')
  const value = server.address().port
  server.close()
  await once(server, 'close')
  return value
}

test('authenticated raw totals reach the Application snapshot with endpoint authorization', { timeout: 10000 }, async () => {
  const httpPort = await port(), udpPort = await port()
  const secret = 'counter-secret-'.padEnd(40, 'x'), token = 'observer-'.padEnd(40, 'x')
  const child = spawn(process.execPath, ['server.mjs'], { cwd: here, env: {
    ...process.env, ...normalizedConfigEnvironment(resolve(here, '../../examples/four-radio-vita/graphx.yml')),
    PORT: String(httpPort), GRAPHX_TELEMETRY_PORT: String(udpPort),
    GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
    GRAPHX_HISTORY_ENABLED: 'false', GRAPHX_CAPTURE_ENABLED: 'false',
    GRAPHX_TELEMETRY_SHARED_SECRET: secret, GRAPHX_OBSERVATION_TOKEN: token,
    GRAPHX_CONTROL_TOKEN: '',
  }, stdio: ['ignore', 'pipe', 'pipe'] })
  const socket = createSocket('udp4')
  let diagnostic = ''
  child.stderr.on('data', data => { diagnostic = (diagnostic + data).slice(-8192) })
  child.stdout.resume()
  const snapshot = async () => {
    const response = await fetch(`http://127.0.0.1:${httpPort}/api/topology`, {
      headers: { Authorization: `Bearer ${token}` },
    })
    assert.equal(response.status, 200, diagnostic)
    return response.json()
  }
  const send = async (value, key = secret) => {
    const bytes = Buffer.from(JSON.stringify(signEnvelope(value, key)))
    await new Promise((yes, no) => socket.send(bytes, udpPort, '127.0.0.1', error => error ? no(error) : yes()))
  }
  try {
    for (let attempt = 0; ; ++attempt) {
      try { await snapshot(); break } catch (error) {
        if (attempt >= 100) throw new Error(`${error}: ${diagnostic}`)
        await new Promise(done => setTimeout(done, 25))
      }
    }
    const now = Date.now()
    const totals = { kind: 'edge_totals', event: 'totals', nodeId: 'radio1', edgeId: 'data1',
      sessionId: 'a'.repeat(32), direction: 'sent', timestamp: now - 1000, packets: 10, wireBytes: 41280 }
    await send(totals)
    await send({ ...totals, timestamp: now, packets: 110, wireBytes: 454080 })
    await send({ ...totals, nodeId: 'processor', direction: 'received', timestamp: now, packets: 109, wireBytes: 449952 })
    // Valid signatures do not authorize an unrelated node or the wrong endpoint.
    await send({ ...totals, nodeId: 'radio2', timestamp: now + 1, packets: 999 })
    await send({ ...totals, nodeId: 'processor', timestamp: now + 1, packets: 999 })
    await send({ ...totals, timestamp: now + 1, packets: 999 }, 'wrong-secret-'.padEnd(40, 'x'))
    let edge
    for (let attempt = 0; attempt < 80; ++attempt) {
      edge = (await snapshot()).edges.data1
      if (edge.sent === 110 && edge.received === 109) break
      await new Promise(done => setTimeout(done, 25))
    }
    assert.equal(edge.sent, 110)
    assert.equal(edge.received, 109)
    assert.equal(edge.messageRate, 100)
    assert.equal(edge.byteRate, 412800)
    assert.equal(edge.meanLatencyUs, null)
    assert.equal(edge.metricSources.counters, 'measured-cumulative')
    assert.equal(edge.metricSources.drops, 'unavailable')
  } finally {
    socket.close()
    if (child.exitCode === null) { child.kill('SIGTERM'); await once(child, 'exit') }
  }
})
