import assert from 'node:assert/strict'
import { createServer } from 'node:http'
import { createServer as createSecureServer } from 'node:https'
import { execFileSync, spawn } from 'node:child_process'
import dgram from 'node:dgram'
import { once } from 'node:events'
import { mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
import { OtlpHttpExporter, SloEvaluator, graphReadiness, nonZeroOtlpId, otlpConfig,
  otlpMetricsRequest, otlpTraceRequest } from './operations.mjs'

async function waitFor(read, expected, attempts = 100) {
  for (let attempt = 0; attempt < attempts && read() !== expected; ++attempt)
    await new Promise(resolve => setTimeout(resolve, 10))
  assert.equal(read(), expected)
}

test('graph readiness reports stale nodes and disconnected edges separately', () => {
  const now = 10_000
  assert.deepEqual(graphReadiness({ a: { status: 'running', lastSeen: 9_000 } },
    { e: { connection: 'connected' } }, now, 2_000).ready, true)
  const result = graphReadiness({ a: { status: 'running', lastSeen: 1_000 } },
    { e: { connection: 'error' } }, now, 2_000)
  assert.equal(result.ready, false)
  assert.deepEqual(result.nodes.failing, ['a'])
  assert.deepEqual(result.edges.failing, ['e'])
})

test('SLO evaluator warms up, evaluates objectives, and bounds its window', () => {
  const evaluator = new SloEvaluator({ window_seconds: 10, minimum_window_seconds: 2,
    availability_target: 0.5, max_error_ratio: 0.1, max_drop_ratio: 0.1,
    max_p95_latency_us: 1000 }, [100, 1000])
  const edge = { sent: 10, received: 10, errors: 0, drops: 0, latencyBuckets: [10, 0, 0] }
  assert.equal(evaluator.observe(true, { e: edge }, 0).status, 'warming')
  assert.equal(evaluator.observe(true, { e: { ...edge, sent: 20, received: 20,
    latencyBuckets: [20, 0, 0] } }, 2_000).status, 'met')
  const violated = evaluator.observe(false, { e: { ...edge, sent: 30, received: 30,
    errors: 20, latencyBuckets: [20, 0, 20] } }, 3_000)
  assert.equal(violated.status, 'violated')
  const reset = evaluator.observe(true, { e: edge }, 4_000)
  assert.ok(reset.traffic >= 20)
  evaluator.observe(true, { e: edge }, 20_000)
  assert.ok(evaluator.samples.length <= 2)
})

test('OTLP JSON uses standard paths, hexadecimal identifiers, and decimal nanoseconds', () => {
  const trace = otlpTraceRequest({ event: 'receive', nodeId: 'sink', edgeId: 'samples',
    traceId: '00112233445566778899aabbccddeeff', spanId: '0011223344556677', timestamp: 1234 })
  const span = trace.resourceSpans[0].scopeSpans[0].spans[0]
  assert.equal(span.traceId, '00112233445566778899aabbccddeeff')
  assert.equal(span.startTimeUnixNano, '1234000000')
  const second = otlpTraceRequest({ event: 'receive', nodeId: 'sink', edgeId: 'samples',
    traceId: '00112233445566778899aabbccddeeff', timestamp: 1234 })
    .resourceSpans[0].scopeSpans[0].spans[0]
  assert.notEqual(second.spanId, span.spanId)
  const metrics = otlpMetricsRequest({ e: { connection: 'connected', messageRate: 3 } }, {},
    { status: 'met', met: true, objectives: { availability: { value: 1, target: 0.99 },
      p95LatencyUs: { value: 1000, target: 10000 } } }, true, 1234)
  assert.equal(metrics.resourceMetrics[0].scopeMetrics[0].metrics[0].gauge.dataPoints[0].asDouble, 1)
  assert.ok(metrics.resourceMetrics[0].scopeMetrics[0].metrics.some(metric =>
    metric.name === 'graphx.slo.latency' && metric.unit === 's'))
  const status = metrics.resourceMetrics[0].scopeMetrics[0].metrics.find(metric =>
    metric.name === 'graphx.slo.status')
  assert.deepEqual(status.gauge.dataPoints.map(value => value.asDouble), [0, 1, 0])
})

test('OTLP fallback identifiers remain non-zero when entropy returns reserved zero values', () => {
  assert.equal(nonZeroOtlpId('0'.repeat(32), 32), `${'0'.repeat(31)}1`)
  let calls = 0
  const zeroThenValid = bytes => {
    calls++
    return calls === 1 ? Buffer.alloc(bytes) : Buffer.from(`${'0'.repeat(bytes * 2 - 1)}1`, 'hex')
  }
  const span = otlpTraceRequest({ event: 'send', timestamp: 1234 },
    'graphx-telemetry', zeroThenValid).resourceSpans[0].scopeSpans[0].spans[0]
  assert.equal(calls, 2)
  assert.equal(span.spanId, `${'0'.repeat(15)}1`)
  assert.doesNotMatch(span.traceId, /^0+$/)
  let zeroCalls = 0
  const boundedFallback = otlpTraceRequest({ event: 'send', timestamp: 1234 },
    'graphx-telemetry', bytes => { zeroCalls++; return Buffer.alloc(bytes) })
    .resourceSpans[0].scopeSpans[0].spans[0]
  assert.equal(zeroCalls, 4)
  assert.equal(boundedFallback.spanId, `${'0'.repeat(15)}1`)
})

test('operations dashboard keeps ratios, seconds, rates, and queue depth on separate panels', () => {
  const dashboard = JSON.parse(readFileSync(resolve(dirname(fileURLToPath(import.meta.url)),
    '../../deploy/observability/grafana/dashboards/graphx-operations.json')))
  for (const panel of dashboard.panels) {
    const expressions = (panel.targets || []).map(target => target.expr).join(' ')
    assert.ok(!(expressions.includes('graphx_slo_ratio') &&
      expressions.includes('graphx_slo_latency_seconds')))
    assert.ok(!(expressions.includes('graphx_otlp_exports_total') &&
      expressions.includes('graphx_otlp_queue_depth')))
  }
  const panel = title => dashboard.panels.find(value => value.title === title)
  assert.match(panel('History backend').targets[0].expr, /graphx_history_backend_up/)
  assert.equal(panel('History outcomes').fieldConfig.defaults.unit, 'ops')
  assert.equal(panel('History storage and queue').fieldConfig.defaults.unit, 'bytes')
  assert.match(panel('Control policy').targets[0].expr, /graphx_control_policy_valid/)
  assert.match(panel('Control command outcomes').targets[0].expr, /graphx_control_commands_total/)
})

test('OTLP configuration rejects credentials and insecure remote endpoints', () => {
  assert.throws(() => otlpConfig({ enabled: true, endpoint: 'http://example.com:4318' }, {}), /plaintext/)
  assert.throws(() => otlpConfig({ enabled: true, endpoint: 'https://user:password@example.com' }, {}), /credentials/)
  assert.throws(() => otlpConfig({ enabled: true, endpoint: 'http://127.0.0.1:4318' },
    { GRAPHX_OTLP_TRACES_PATH: `/${'x'.repeat(256)}` }), /paths/)
  const configured = otlpConfig({ enabled: true, endpoint: 'http://127.0.0.1:4318' },
    { GRAPHX_OTLP_ENDPOINT: '' })
  assert.equal(configured.enabled, true)
  assert.equal(configured.tracesPath, '/v1/traces')
  assert.throws(() => otlpConfig({ enabled: true, endpoint: 'http://127.0.0.1:4318' },
    { GRAPHX_OTLP_QUEUE_CAPACITY: '1.5' }), /integer/)
  assert.throws(() => new SloEvaluator({ window_seconds: 10.5 }), /integer/)
})

test('bounded OTLP exporter posts JSON and records partial success', async () => {
  const requests = []
  const server = createServer((request, response) => {
    let body = ''
    request.on('data', data => { body += data })
    request.on('end', () => { requests.push({ path: request.url, body,
      authorization: request.headers.authorization }); response.writeHead(200, { 'content-type': 'application/json' });
    response.end('{"partialSuccess":{"rejectedSpans":"2"}}') })
  })
  server.listen(0, '127.0.0.1'); await once(server, 'listening')
  const exporter = new OtlpHttpExporter({ enabled: true,
    endpoint: new URL(`http://127.0.0.1:${server.address().port}`), queueCapacity: 2,
    maxQueueBytes: 65536, timeoutMs: 1000, maxResponseBytes: 1024 })
  exporter.config.token = 'a-secure-token-value'
  exporter.enqueue('/v1/traces', { resourceSpans: [] })
  for (let attempt = 0; attempt < 50 && exporter.stats.exported === 0; ++attempt)
    await new Promise(resolve => setTimeout(resolve, 10))
  assert.equal(exporter.stats.exported, 1)
  assert.equal(exporter.stats.rejected, 2)
  assert.equal(requests[0].path, '/v1/traces')
  assert.deepEqual(JSON.parse(requests[0].body), { resourceSpans: [] })
  assert.equal(requests[0].authorization, 'Bearer a-secure-token-value')
  exporter.close(); server.close(); await once(server, 'close')
})

test('OTLP exporter retries transient status with bounded backoff and not permanent errors', async () => {
  let transientRequests = 0
  let permanentRequests = 0
  const server = createServer((request, response) => {
    request.resume()
    if (request.url === '/transient' && ++transientRequests === 1) {
      response.writeHead(503, { 'retry-after': '0' }); response.end(); return
    }
    if (request.url === '/permanent') {
      permanentRequests++; response.writeHead(400); response.end(); return
    }
    response.writeHead(200, { 'content-type': 'application/json' }); response.end('{}')
  })
  server.listen(0, '127.0.0.1'); await once(server, 'listening')
  const config = { enabled: true, endpoint: new URL(`http://127.0.0.1:${server.address().port}`),
    queueCapacity: 2, maxQueueBytes: 65536, timeoutMs: 1000, maxResponseBytes: 1024,
    retryMaxAttempts: 3, retryInitialBackoffMs: 10, retryMaxBackoffMs: 20 }
  const transient = new OtlpHttpExporter(config, { random: () => 0 })
  transient.enqueue('/transient', {})
  await waitFor(() => transient.stats.exported, 1)
  assert.equal(transientRequests, 2)
  assert.equal(transient.stats.retried, 1)
  const permanent = new OtlpHttpExporter(config)
  permanent.enqueue('/permanent', {})
  await waitFor(() => permanent.stats.failed, 1)
  assert.equal(permanentRequests, 1)
  assert.equal(permanent.stats.retried, 0)
  transient.close(); permanent.close(); server.close(); await once(server, 'close')
})

test('OTLP exporter recovers when a refused collector starts during bounded retry',
  { timeout: 5000 }, async () => {
    const probe = createServer()
    probe.listen(0, '127.0.0.1'); await once(probe, 'listening')
    const port = probe.address().port
    probe.close(); await once(probe, 'close')
    const exporter = new OtlpHttpExporter({ enabled: true,
      endpoint: new URL(`http://127.0.0.1:${port}`), queueCapacity: 2,
      maxQueueBytes: 65536, timeoutMs: 200, maxResponseBytes: 1024,
      retryMaxAttempts: 5, retryInitialBackoffMs: 100, retryMaxBackoffMs: 200 },
    { random: () => 0 })
    const receiver = createServer((request, response) => {
      request.resume(); response.writeHead(200, { 'content-type': 'application/json' }); response.end('{}')
    })
    try {
      exporter.enqueue('/recover', {})
      await waitFor(() => Math.min(exporter.stats.retried, 1), 1)
      receiver.listen(port, '127.0.0.1'); await once(receiver, 'listening')
      await waitFor(() => exporter.stats.exported, 1)
      assert.ok(exporter.stats.retried >= 1)
      assert.equal(exporter.stats.failed, 0)
    } finally {
      exporter.close()
      if (receiver.listening) { receiver.close(); await once(receiver, 'close') }
    }
  })

test('OTLP exporter preserves queue limits while an active item waits for retry', async () => {
  const server = createServer((request, response) => {
    request.resume(); response.writeHead(503, { 'retry-after': '1' }); response.end()
  })
  server.listen(0, '127.0.0.1'); await once(server, 'listening')
  const exporter = new OtlpHttpExporter({ enabled: true,
    endpoint: new URL(`http://127.0.0.1:${server.address().port}`), queueCapacity: 1,
    maxQueueBytes: 65536, timeoutMs: 200, maxResponseBytes: 1024,
    retryMaxAttempts: 3, retryInitialBackoffMs: 10, retryMaxBackoffMs: 1000 })
  try {
    assert.equal(exporter.enqueue('/first', {}), true)
    await waitFor(() => exporter.stats.retried, 1)
    assert.equal(exporter.enqueue('/second', {}), true)
    assert.equal(exporter.enqueue('/third', {}), false)
    assert.equal(exporter.stats.queueDepth, 1)
    assert.equal(exporter.stats.dropped, 1)
    assert.ok(exporter.stats.queueBytes <= exporter.config.maxQueueBytes)
  } finally {
    exporter.close(); server.close(); await once(server, 'close')
  }
})

test('OTLP exporter enforces byte capacity before opening a request', () => {
  const exporter = new OtlpHttpExporter({ enabled: true, endpoint: new URL('http://127.0.0.1:1'),
    queueCapacity: 10, maxQueueBytes: 10, timeoutMs: 100, maxResponseBytes: 1024 })
  assert.equal(exporter.enqueue('/v1/traces', { payload: 'larger-than-ten-bytes' }), false)
  assert.equal(exporter.stats.dropped, 1)
  assert.equal(exporter.stats.queueBytes, 0)
  exporter.close()
})

test('OTLP exporter bounds response bodies and request time', async () => {
  const server = createServer((request, response) => {
    if (request.url === '/large') { response.writeHead(200, { 'content-type': 'application/json' }); response.end('x'.repeat(2048)) }
    // Intentionally leave /slow open until the client deadline destroys it.
  })
  server.listen(0, '127.0.0.1'); await once(server, 'listening')
  const exporter = new OtlpHttpExporter({ enabled: true,
    endpoint: new URL(`http://127.0.0.1:${server.address().port}`), queueCapacity: 2,
    maxQueueBytes: 65536, timeoutMs: 100, maxResponseBytes: 1024, retryMaxAttempts: 1 })
  exporter.enqueue('/large', {})
  exporter.enqueue('/slow', {})
  for (let attempt = 0; attempt < 50 && exporter.stats.failed < 2; ++attempt)
    await new Promise(resolve => setTimeout(resolve, 10))
  assert.equal(exporter.stats.failed, 2)
  assert.match(exporter.stats.lastError, /deadline exceeded/)
  exporter.close(); server.closeAllConnections(); server.close(); await once(server, 'close')
})

test('OTLP absolute deadline defeats a response that continuously trickles bytes', async () => {
  const intervals = new Set()
  const server = createServer((request, response) => {
    response.writeHead(200, { 'content-type': 'application/json' })
    const interval = setInterval(() => response.write(' '), 20)
    intervals.add(interval)
    request.on('close', () => { clearInterval(interval); intervals.delete(interval) })
  })
  server.listen(0, '127.0.0.1'); await once(server, 'listening')
  const exporter = new OtlpHttpExporter({ enabled: true,
    endpoint: new URL(`http://127.0.0.1:${server.address().port}`), queueCapacity: 1,
    maxQueueBytes: 65536, timeoutMs: 100, maxResponseBytes: 1024, retryMaxAttempts: 1 })
  const started = Date.now()
  exporter.enqueue('/trickle', {})
  await waitFor(() => exporter.stats.failed, 1)
  assert.ok(Date.now() - started < 500)
  assert.match(exporter.stats.lastError, /deadline exceeded/)
  exporter.close()
  for (const interval of intervals) clearInterval(interval)
  server.closeAllConnections(); server.close(); await once(server, 'close')
})

test('OTLP shutdown interrupts retry backoff without recording a terminal failure', async () => {
  const server = createServer((request, response) => {
    request.resume(); response.writeHead(503, { 'retry-after': '60' }); response.end()
  })
  server.listen(0, '127.0.0.1'); await once(server, 'listening')
  const exporter = new OtlpHttpExporter({ enabled: true,
    endpoint: new URL(`http://127.0.0.1:${server.address().port}`), queueCapacity: 1,
    maxQueueBytes: 65536, timeoutMs: 1000, maxResponseBytes: 1024,
    retryMaxAttempts: 3, retryInitialBackoffMs: 10, retryMaxBackoffMs: 60000 })
  exporter.enqueue('/retry', {})
  await waitFor(() => exporter.stats.retried, 1)
  exporter.close()
  await new Promise(resolve => setTimeout(resolve, 20))
  assert.equal(exporter.running, false)
  assert.equal(exporter.stats.failed, 0)
  server.close(); await once(server, 'close')
})

test('OTLP private CA and mTLS accept a client certificate and reject its absence',
  { timeout: 10000 }, async () => {
    const directory = mkdtempSync(join(tmpdir(), 'graphx-otlp-mtls-'))
    const openssl = (...arguments_) => execFileSync('openssl', arguments_, { stdio: 'ignore' })
    try {
      openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
        '-subj', '/CN=GraphX Test CA', '-keyout', join(directory, 'ca.key'),
        '-out', join(directory, 'ca.pem'))
      openssl('req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=127.0.0.1',
        '-addext', 'subjectAltName=IP:127.0.0.1', '-addext', 'extendedKeyUsage=serverAuth',
        '-keyout', join(directory, 'server.key'), '-out', join(directory, 'server.csr'))
      openssl('x509', '-req', '-days', '1', '-in', join(directory, 'server.csr'),
        '-CA', join(directory, 'ca.pem'), '-CAkey', join(directory, 'ca.key'), '-CAcreateserial',
        '-copy_extensions', 'copy', '-out', join(directory, 'server.pem'))
      openssl('req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=graphx-client',
        '-addext', 'extendedKeyUsage=clientAuth', '-keyout', join(directory, 'client.key'),
        '-out', join(directory, 'client.csr'))
      openssl('x509', '-req', '-days', '1', '-in', join(directory, 'client.csr'),
        '-CA', join(directory, 'ca.pem'), '-CAkey', join(directory, 'ca.key'), '-CAcreateserial',
        '-copy_extensions', 'copy', '-out', join(directory, 'client.pem'))
      let authorizedRequests = 0
      const server = createSecureServer({ key: readFileSync(join(directory, 'server.key')),
        cert: readFileSync(join(directory, 'server.pem')), ca: readFileSync(join(directory, 'ca.pem')),
        requestCert: true, rejectUnauthorized: true }, (request, response) => {
        if (request.socket.authorized) authorizedRequests++
        request.resume(); request.on('end', () => {
          response.writeHead(200, { 'content-type': 'application/json' }); response.end('{}')
        })
      })
      server.listen(0, '127.0.0.1'); await once(server, 'listening')
      const endpoint = `https://127.0.0.1:${server.address().port}`
      const good = new OtlpHttpExporter(otlpConfig({ enabled: true, endpoint }, {
        GRAPHX_OTLP_CA_FILE: join(directory, 'ca.pem'),
        GRAPHX_OTLP_CERT_FILE: join(directory, 'client.pem'),
        GRAPHX_OTLP_KEY_FILE: join(directory, 'client.key'),
        GRAPHX_OTLP_RETRY_MAX_ATTEMPTS: '1',
      }))
      good.enqueue('/v1/traces', {})
      await waitFor(() => good.stats.exported, 1)
      const bad = new OtlpHttpExporter(otlpConfig({ enabled: true, endpoint }, {
        GRAPHX_OTLP_CA_FILE: join(directory, 'ca.pem'), GRAPHX_OTLP_RETRY_MAX_ATTEMPTS: '1',
      }))
      bad.enqueue('/v1/traces', {})
      await waitFor(() => bad.stats.failed, 1)
      assert.equal(authorizedRequests, 1)
      assert.match(bad.stats.lastError, /certificate required|socket disconnected|alert/i)
      good.close(); bad.close(); server.closeAllConnections(); server.close(); await once(server, 'close')
    } finally { rmSync(directory, { recursive: true, force: true }) }
  })

async function availableUdpPort() {
  const socket = dgram.createSocket('udp4')
  socket.bind(0, '127.0.0.1'); await once(socket, 'listening')
  const port = socket.address().port
  socket.close(); await once(socket, 'close')
  return port
}

test('telemetry service converts validated UDP events to authenticated OTLP', { timeout: 10000 }, async () => {
  let resolveExport
  const exported = new Promise(resolveRequest => { resolveExport = resolveRequest })
  const collector = createServer((request, response) => {
    let body = ''
    request.on('data', data => { body += data })
    request.on('end', () => {
      resolveExport({ path: request.url, authorization: request.headers.authorization, body })
      response.writeHead(200, { 'content-type': 'application/json' }); response.end('{}')
    })
  })
  collector.listen(0, '127.0.0.1'); await once(collector, 'listening')
  const api = createServer(); api.listen(0, '127.0.0.1'); await once(api, 'listening')
  const apiPort = api.address().port; api.close(); await once(api, 'close')
  const udpPort = await availableUdpPort()
  const directory = dirname(fileURLToPath(import.meta.url))
  const token = '0123456789abcdef0123456789abcdef'
  const child = spawn(process.execPath, ['server.mjs'], { cwd: directory, stdio: 'ignore', env: {
    ...process.env, PORT: `${apiPort}`, GRAPHX_TELEMETRY_PORT: `${udpPort}`,
    GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
    GRAPHX_CONFIG: resolve(directory, '../../graphx.yaml'),
    GRAPHX_OTLP_ENDPOINT: `http://127.0.0.1:${collector.address().port}`,
    GRAPHX_OTLP_AUTH_TOKEN: token, GRAPHX_OTLP_EXPORT_INTERVAL_MS: '600000',
  } })
  const sender = dgram.createSocket('udp4')
  try {
    for (let attempt = 0; attempt < 50; ++attempt) {
      try { if ((await fetch(`http://127.0.0.1:${apiPort}/api/ready`)).ok) break }
      catch { /* child is starting */ }
      await new Promise(resolveWait => setTimeout(resolveWait, 20))
    }
    const event = { kind: 'trace', event: 'send', nodeId: 'generator', edgeId: 'samples',
      timestamp: Date.now(), sequence: 7, wireBytes: 128,
      traceId: '00112233445566778899aabbccddeeff', messageId: 'message-7' }
    sender.send(Buffer.from(JSON.stringify(event)), udpPort, '127.0.0.1')
    let timeout
    const result = await Promise.race([exported,
      new Promise((_, reject) => { timeout = setTimeout(() => reject(new Error('OTLP export not received')), 2000) })])
    clearTimeout(timeout)
    assert.equal(result.path, '/v1/traces')
    assert.equal(result.authorization, `Bearer ${token}`)
    const span = JSON.parse(result.body).resourceSpans[0].scopeSpans[0].spans[0]
    assert.equal(span.traceId, event.traceId)
  } finally {
    sender.close(); child.kill('SIGTERM')
    if (child.exitCode == null) await once(child, 'exit')
    collector.close(); await once(collector, 'close')
  }
})
