import { readFileSync, statSync } from 'node:fs'
import { createHash, randomBytes } from 'node:crypto'
import http from 'node:http'
import https from 'node:https'
import { isLoopback, readSecret } from './security.mjs'

const MAX_OTLP_RESPONSE_BYTES = 4 * 1024 * 1024
const MAX_TLS_FILE_BYTES = 1024 * 1024

function readTlsFile(path, name) {
  if (!path) return undefined
  if (statSync(path).size > MAX_TLS_FILE_BYTES) throw new Error(`${name} exceeds 1048576 bytes`)
  return readFileSync(path)
}

function boundedNumber(value, fallback, minimum, maximum, integer = false) {
  if (value == null || value === '') return fallback
  const parsed = Number(value)
  if (!Number.isFinite(parsed) || parsed < minimum || parsed > maximum ||
      (integer && !Number.isSafeInteger(parsed)))
    throw new Error(`operational value must be ${integer ? 'an integer ' : ''}between ${minimum} and ${maximum}`)
  return parsed
}

export function graphReadiness(nodes, edges, now = Date.now(), heartbeatTimeoutMs = 5000) {
  const nodeFailures = Object.entries(nodes).filter(([, node]) =>
    node.status !== 'running' || !node.lastSeen || now - node.lastSeen > heartbeatTimeoutMs)
    .map(([id]) => id)
  const edgeFailures = Object.entries(edges).filter(([, edge]) => edge.connection !== 'connected')
    .map(([id]) => id)
  return { ready: nodeFailures.length === 0 && edgeFailures.length === 0,
    nodes: { ready: Object.keys(nodes).length - nodeFailures.length, total: Object.keys(nodes).length,
      failing: nodeFailures },
    edges: { ready: Object.keys(edges).length - edgeFailures.length, total: Object.keys(edges).length,
      failing: edgeFailures } }
}

function histogramPercentile(buckets, bounds, percentile) {
  const count = buckets.reduce((sum, value) => sum + value, 0)
  if (!count) return null
  const target = Math.ceil(count * percentile)
  let cumulative = 0
  for (let index = 0; index < buckets.length; ++index) {
    cumulative += buckets[index]
    if (cumulative >= target) return index < bounds.length ? bounds[index] : bounds.at(-1)
  }
  return bounds.at(-1)
}

export class SloEvaluator {
  constructor(config = {}, latencyBoundsUs = [10, 50, 100, 500, 1000, 5000, 10000]) {
    this.config = {
      windowSeconds: boundedNumber(config.window_seconds, 300, 10, 3600, true),
      minimumWindowSeconds: boundedNumber(config.minimum_window_seconds, 10, 1, 3600, true),
      availabilityTarget: boundedNumber(config.availability_target, 0.99, 0, 1),
      maxErrorRatio: boundedNumber(config.max_error_ratio, 0.01, 0, 1),
      maxDropRatio: boundedNumber(config.max_drop_ratio, 0.01, 0, 1),
      maxP95LatencyUs: boundedNumber(config.max_p95_latency_us, 10000, 1, 3_600_000_000, true),
    }
    if (this.config.minimumWindowSeconds > this.config.windowSeconds)
      throw new Error('SLO minimum_window_seconds must not exceed window_seconds')
    this.latencyBoundsUs = latencyBoundsUs
    this.maxSamples = Math.ceil(this.config.windowSeconds) + 2
    this.samples = []
    this.previousTotals = null
  }

  observe(ready, edges, timestamp = Date.now()) {
    const totals = { traffic: 0, errors: 0, drops: 0,
      latency: Array(this.latencyBoundsUs.length + 1).fill(0) }
    for (const edge of Object.values(edges)) {
      totals.traffic += Number(edge.sent || 0) + Number(edge.received || 0)
      totals.errors += Number(edge.errors || 0)
      totals.drops += Number(edge.drops || 0)
      edge.latencyBuckets?.forEach((value, index) => { totals.latency[index] += Number(value || 0) })
    }
    const delta = (current, previous) => current >= previous ? current - previous : current
    const sample = { timestamp, ready: Boolean(ready), traffic: 0, errors: 0, drops: 0,
      latency: Array(this.latencyBoundsUs.length + 1).fill(0) }
    if (this.previousTotals) {
      sample.traffic = delta(totals.traffic, this.previousTotals.traffic)
      sample.errors = delta(totals.errors, this.previousTotals.errors)
      sample.drops = delta(totals.drops, this.previousTotals.drops)
      sample.latency = totals.latency.map((value, index) =>
        delta(value, this.previousTotals.latency[index]))
    }
    this.previousTotals = totals
    this.samples.push(sample)
    const cutoff = timestamp - this.config.windowSeconds * 1000
    while (this.samples.length > 1 && this.samples[0].timestamp < cutoff) this.samples.shift()
    while (this.samples.length > this.maxSamples) this.samples.shift()
    return this.snapshot(timestamp)
  }

  snapshot(timestamp = Date.now()) {
    const first = this.samples[0]
    const last = this.samples.at(-1)
    const observedSeconds = first && last ? Math.max(0, (last.timestamp - first.timestamp) / 1000) : 0
    const availability = this.samples.length ? this.samples.filter(sample => sample.ready).length / this.samples.length : null
    const total = (key) => this.samples.reduce((sum, sample) => sum + sample[key], 0)
    const traffic = total('traffic')
    const errors = total('errors')
    const drops = total('drops')
    const latency = Array(this.latencyBoundsUs.length + 1).fill(0)
    for (const sample of this.samples)
      sample.latency.forEach((value, index) => { latency[index] += value })
    const p95LatencyUs = histogramPercentile(latency, this.latencyBoundsUs, 0.95)
    const errorRatio = traffic ? errors / traffic : 0
    const dropRatio = traffic ? drops / traffic : 0
    const warming = observedSeconds < this.config.minimumWindowSeconds
    const objectives = {
      availability: { value: availability, target: this.config.availabilityTarget,
        met: availability != null && availability >= this.config.availabilityTarget },
      errorRatio: { value: errorRatio, target: this.config.maxErrorRatio,
        met: errorRatio <= this.config.maxErrorRatio },
      dropRatio: { value: dropRatio, target: this.config.maxDropRatio,
        met: dropRatio <= this.config.maxDropRatio },
      p95LatencyUs: { value: p95LatencyUs, target: this.config.maxP95LatencyUs,
        met: p95LatencyUs == null || p95LatencyUs <= this.config.maxP95LatencyUs },
    }
    const met = Object.values(objectives).every(objective => objective.met)
    return { status: warming ? 'warming' : met ? 'met' : 'violated', met: !warming && met,
      windowSeconds: this.config.windowSeconds, observedSeconds, sampleCount: this.samples.length,
      traffic, objectives, evaluatedAt: new Date(timestamp).toISOString() }
  }
}

function unixNano(timestamp) {
  const milliseconds = Math.trunc(timestamp)
  const fractionalNanos = Math.round((timestamp - milliseconds) * 1_000_000)
  return `${BigInt(milliseconds) * 1_000_000n + BigInt(fractionalNanos)}`
}

export function nonZeroOtlpId(value, length) {
  if (!Number.isSafeInteger(length) || length < 1 ||
      typeof value !== 'string' || value.length !== length || !/^[0-9a-f]+$/i.test(value))
    throw new Error(`OTLP identifier must contain exactly ${length} hexadecimal characters`)
  return /^0+$/.test(value) ? `${'0'.repeat(length - 1)}1` : value.toLowerCase()
}

function randomNonZeroOtlpId(length, randomSource) {
  for (let attempt = 0; attempt < 4; ++attempt) {
    const candidate = randomSource(length / 2).toString('hex')
    if (!/^0+$/.test(candidate)) return nonZeroOtlpId(candidate, length)
  }
  return `${'0'.repeat(length - 1)}1`
}

export function otlpTraceRequest(event, serviceName = 'graphx-telemetry', randomSource = randomBytes) {
  const timestamp = Number(event.timestamp || Date.now())
  const startTimestamp = Math.max(0, timestamp - (Number(event.latencyUs) || 0) / 1000)
  const identity = `${event.nodeId || ''}:${event.edgeId || ''}:${event.event || ''}:${event.sequence || 0}:${timestamp}`
  const digest = createHash('sha256').update(identity).digest('hex')
  const traceId = /^[0-9a-f]{32}$/i.test(event.traceId || '') && !/^0+$/.test(event.traceId) ? event.traceId :
    nonZeroOtlpId(digest.slice(0, 32), 32)
  const spanId = /^[0-9a-f]{16}$/i.test(event.spanId || '') && !/^0+$/.test(event.spanId) ? event.spanId :
    randomNonZeroOtlpId(16, randomSource)
  const attributes = [
    { key: 'graphx.node.id', value: { stringValue: event.nodeId || '' } },
    { key: 'graphx.edge.id', value: { stringValue: event.edgeId || '' } },
    { key: 'graphx.event', value: { stringValue: event.event || '' } },
  ]
  for (const [key, value] of [['graphx.message.id', event.messageId],
    ['graphx.parent_message.id', event.parentMessageId]])
    if (value) attributes.push({ key, value: { stringValue: value } })
  for (const [key, value] of [['graphx.sequence', event.sequence],
    ['graphx.wire_bytes', event.wireBytes], ['graphx.latency_us', event.latencyUs]])
    if (Number.isFinite(value)) attributes.push({ key, value: { intValue: `${Math.trunc(value)}` } })
  const sourceServiceName = serviceName === 'graphx-telemetry' && event.nodeId ?
    `graphx-${event.nodeId}` : serviceName
  return { resourceSpans: [{ resource: { attributes: [
    { key: 'service.name', value: { stringValue: sourceServiceName } },
    { key: 'service.instance.id', value: { stringValue: event.nodeId || 'unknown' } },
  ] }, scopeSpans: [{ scope: { name: 'graphx.telemetry' }, spans: [{
    traceId: traceId.toLowerCase(), spanId: spanId.toLowerCase(), name: `graphx.${event.event || 'event'}`,
    kind: event.event === 'receive' ? 5 : event.event === 'send' ? 4 : 1,
    startTimeUnixNano: unixNano(startTimestamp), endTimeUnixNano: unixNano(timestamp),
    attributes, status: { code: event.event === 'error' ? 2 : 1 },
  }] }] }] }
}

export function otlpMetricsRequest(edges, nodes, slo, graphReady = false, timestamp = Date.now(), serviceName = 'graphx-telemetry') {
  const timeUnixNano = unixNano(timestamp)
  const gauge = (name, description, points) => ({ name, description, gauge: { dataPoints: points } })
  const point = (value, attributes = []) => ({ timeUnixNano, asDouble: Number(value), attributes })
  const label = (key, value) => ({ key, value: { stringValue: value } })
  const edgePoints = (read) => Object.entries(edges).map(([id, edge]) => point(read(edge),
    [{ key: 'graphx.edge.id', value: { stringValue: id } }]))
  const sloRatioPoints = []
  for (const [name, objective] of Object.entries(slo.objectives || {})) {
    if (name === 'p95LatencyUs') continue
    if (objective.value != null) sloRatioPoints.push(point(objective.value,
      [label('graphx.slo.objective', name), label('graphx.slo.kind', 'value')]))
    sloRatioPoints.push(point(objective.target,
      [label('graphx.slo.objective', name), label('graphx.slo.kind', 'target')]))
  }
  const latency = slo.objectives?.p95LatencyUs
  const sloLatencyPoints = latency ? [
    ...(latency.value == null ? [] : [point(latency.value / 1e6, [label('graphx.slo.kind', 'value')])]),
    point(latency.target / 1e6, [label('graphx.slo.kind', 'target')]),
  ] : []
  const metrics = [
    gauge('graphx.edge.connected', 'Whether the GraphX edge is connected.', edgePoints(edge => edge.connection === 'connected' ? 1 : 0)),
    gauge('graphx.edge.message_rate', 'Current GraphX edge message rate.', edgePoints(edge => edge.messageRate || 0)),
    gauge('graphx.slo.met', 'Whether all GraphX SLO objectives are met.', [point(slo.met ? 1 : 0)]),
    gauge('graphx.slo.status', 'Current GraphX SLO evaluator state as a one-hot gauge.',
      ['warming', 'met', 'violated'].map(status => point(slo.status === status ? 1 : 0,
        [label('graphx.slo.status', status)]))),
    gauge('graphx.slo.ratio', 'Current values and targets for dimensionless GraphX SLO objectives.', sloRatioPoints),
    { ...gauge('graphx.slo.latency', 'Current value and target for the GraphX p95 latency SLO.', sloLatencyPoints), unit: 's' },
    gauge('graphx.graph.ready', 'Whether all configured GraphX nodes and edges are ready.', [point(graphReady ? 1 : 0)]),
    gauge('graphx.node.cpu_percent', 'GraphX node CPU percentage.', Object.entries(nodes)
      .filter(([, node]) => Number.isFinite(node.cpuPercent)).map(([id, node]) => point(node.cpuPercent,
        [{ key: 'graphx.node.id', value: { stringValue: id } }]))),
  ]
  return { resourceMetrics: [{ resource: { attributes: [
    { key: 'service.name', value: { stringValue: serviceName } },
  ] }, scopeMetrics: [{ scope: { name: 'graphx.telemetry' }, metrics }] }] }
}

export function otlpConfig(config = {}, env = process.env) {
  const environmentEndpoint = env.GRAPHX_OTLP_ENDPOINT?.trim()
  const enabled = environmentEndpoint ? true : Boolean(config.enabled)
  const endpoint = environmentEndpoint || config.endpoint || 'http://127.0.0.1:4318'
  if (endpoint.length > 2048) throw new Error('OTLP endpoint exceeds 2048 characters')
  const parsed = new URL(endpoint)
  if (!['http:', 'https:'].includes(parsed.protocol)) throw new Error('OTLP endpoint must use http or https')
  if (parsed.username || parsed.password) throw new Error('OTLP endpoint must not contain credentials')
  if (parsed.pathname !== '/' || parsed.search || parsed.hash)
    throw new Error('OTLP endpoint must be an origin without a path, query, or fragment')
  const tracesPath = env.GRAPHX_OTLP_TRACES_PATH || config.traces_path || '/v1/traces'
  const metricsPath = env.GRAPHX_OTLP_METRICS_PATH || config.metrics_path || '/v1/metrics'
  if (tracesPath.length > 256 || metricsPath.length > 256 ||
      !/^\/[A-Za-z0-9._~/-]*$/.test(tracesPath) || !/^\/[A-Za-z0-9._~/-]*$/.test(metricsPath))
    throw new Error('OTLP signal paths contain unsupported characters')
  if (parsed.protocol === 'http:' && !isLoopback(parsed.hostname) && env.GRAPHX_ALLOW_INSECURE_OTLP !== 'true')
    throw new Error('plaintext OTLP endpoint must be loopback or explicitly allowed')
  if (Boolean(env.GRAPHX_OTLP_CERT_FILE) !== Boolean(env.GRAPHX_OTLP_KEY_FILE))
    throw new Error('GRAPHX_OTLP_CERT_FILE and GRAPHX_OTLP_KEY_FILE must be provided together')
  return { enabled, endpoint: parsed,
    tracesPath, metricsPath,
    exportIntervalMs: boundedNumber(env.GRAPHX_OTLP_EXPORT_INTERVAL_MS || config.export_interval_ms,
      5000, 250, 600000, true),
    timeoutMs: boundedNumber(env.GRAPHX_OTLP_TIMEOUT_MS || config.timeout_ms,
      2000, 100, 60000, true),
    queueCapacity: boundedNumber(env.GRAPHX_OTLP_QUEUE_CAPACITY || config.queue_capacity,
      1024, 1, 65536, true),
    maxQueueBytes: boundedNumber(env.GRAPHX_OTLP_MAX_QUEUE_BYTES || config.max_queue_bytes,
      8 * 1024 * 1024, 65536, 64 * 1024 * 1024, true),
    maxResponseBytes: boundedNumber(env.GRAPHX_OTLP_MAX_RESPONSE_BYTES || config.max_response_bytes,
      65536, 1024, MAX_OTLP_RESPONSE_BYTES, true),
    retryMaxAttempts: boundedNumber(env.GRAPHX_OTLP_RETRY_MAX_ATTEMPTS || config.retry_max_attempts,
      3, 1, 10, true),
    retryInitialBackoffMs: boundedNumber(
      env.GRAPHX_OTLP_RETRY_INITIAL_BACKOFF_MS || config.retry_initial_backoff_ms,
      200, 10, 60000, true),
    retryMaxBackoffMs: boundedNumber(env.GRAPHX_OTLP_RETRY_MAX_BACKOFF_MS || config.retry_max_backoff_ms,
      5000, 10, 600000, true),
    token: readSecret('GRAPHX_OTLP_AUTH_TOKEN', env),
    ca: readTlsFile(env.GRAPHX_OTLP_CA_FILE, 'GRAPHX_OTLP_CA_FILE'),
    cert: readTlsFile(env.GRAPHX_OTLP_CERT_FILE, 'GRAPHX_OTLP_CERT_FILE'),
    key: readTlsFile(env.GRAPHX_OTLP_KEY_FILE, 'GRAPHX_OTLP_KEY_FILE'),
  }
}

class OtlpExportError extends Error {
  constructor(message, retryable = false, retryAfterMs = null) {
    super(message)
    this.retryable = retryable
    this.retryAfterMs = retryAfterMs
  }
}

function retryAfterMilliseconds(value, now = Date.now()) {
  if (value == null) return null
  const seconds = Number(value)
  if (Number.isFinite(seconds) && seconds >= 0) return Math.round(seconds * 1000)
  const date = Date.parse(value)
  return Number.isFinite(date) ? Math.max(0, date - now) : null
}

export class OtlpHttpExporter {
  constructor(config, { random = Math.random } = {}) {
    this.config = { retryMaxAttempts: 3, retryInitialBackoffMs: 200,
      retryMaxBackoffMs: 5000, ...config }
    if (this.config.retryMaxBackoffMs < this.config.retryInitialBackoffMs)
      throw new Error('OTLP retry maximum backoff must not be less than initial backoff')
    this.random = random
    this.queue = []
    this.queuedBytes = 0
    this.activeRequests = new Set()
    this.retryWaiters = new Set()
    this.running = false
    this.closed = false
    this.stats = { enqueued: 0, exported: 0, failed: 0, retried: 0, dropped: 0, rejected: 0,
      queueDepth: 0, queueBytes: 0, lastSuccessAt: null, lastError: null }
  }
  enqueue(path, payload) {
    if (!this.config.enabled || this.closed) return false
    const body = Buffer.from(JSON.stringify(payload))
    if (this.queue.length >= this.config.queueCapacity || this.queuedBytes + body.length > this.config.maxQueueBytes) {
      this.stats.dropped++; return false
    }
    this.queue.push({ path, body }); this.queuedBytes += body.length
    this.stats.enqueued++; this.stats.queueDepth = this.queue.length; this.stats.queueBytes = this.queuedBytes
    this.#drain(); return true
  }
  async #drain() {
    if (this.running || this.closed) return
    this.running = true
    while (this.queue.length && !this.closed) {
      const item = this.queue.shift(); this.queuedBytes -= item.body.length
      this.stats.queueDepth = this.queue.length; this.stats.queueBytes = this.queuedBytes
      try {
        const rejected = await this.#sendWithRetry(item)
        if (this.closed) break
        this.stats.exported++; this.stats.rejected += rejected
        this.stats.lastSuccessAt = new Date().toISOString(); this.stats.lastError = null
      }
      catch (error) {
        if (!this.closed) {
          this.stats.failed++
          this.stats.lastError = String(error?.message || 'export failed').slice(0, 256)
        }
      }
    }
    this.running = false
  }
  async #sendWithRetry(item) {
    let lastError
    for (let attempt = 1; attempt <= this.config.retryMaxAttempts && !this.closed; ++attempt) {
      try { return await this.#send(item) }
      catch (error) {
        lastError = error
        if (!error?.retryable || attempt >= this.config.retryMaxAttempts) throw error
        this.stats.retried++
        const exponential = Math.min(this.config.retryMaxBackoffMs,
          this.config.retryInitialBackoffMs * (2 ** (attempt - 1)))
        const jittered = Math.round(exponential * (0.5 + this.random() * 0.5))
        const delay = error.retryAfterMs == null ? jittered :
          Math.min(this.config.retryMaxBackoffMs, error.retryAfterMs)
        if (!await this.#waitForRetry(delay)) throw new OtlpExportError('OTLP exporter closed')
      }
    }
    throw lastError || new OtlpExportError('OTLP exporter closed')
  }
  #waitForRetry(milliseconds) {
    if (this.closed) return Promise.resolve(false)
    return new Promise(resolve => {
      let timer
      const finish = value => {
        clearTimeout(timer); this.retryWaiters.delete(cancel); resolve(value)
      }
      const cancel = () => finish(false)
      timer = setTimeout(() => finish(true), milliseconds)
      this.retryWaiters.add(cancel)
    })
  }
  #send(item) {
    return new Promise((resolve, reject) => {
      let settled = false
      let request
      const finish = (callback, value) => {
        if (settled) return
        settled = true; clearTimeout(deadline); callback(value)
      }
      const fail = error => finish(reject, error instanceof OtlpExportError ? error :
        new OtlpExportError(String(error?.message || 'OTLP request failed'), true))
      const deadline = setTimeout(() => request?.destroy(
        new OtlpExportError('OTLP request deadline exceeded', true)), this.config.timeoutMs)
      const secure = this.config.endpoint.protocol === 'https:'
      request = (secure ? https : http).request({
        protocol: this.config.endpoint.protocol, hostname: this.config.endpoint.hostname,
        port: this.config.endpoint.port || (secure ? 443 : 80), path: item.path, method: 'POST',
        headers: { 'content-type': 'application/json', 'content-length': item.body.length,
          ...(this.config.token ? { authorization: `Bearer ${this.config.token}` } : {}) },
        ca: this.config.ca, cert: this.config.cert, key: this.config.key,
      }, response => {
        let bytes = 0
        const chunks = []
        response.on('data', chunk => {
          bytes += chunk.length
          if (bytes > this.config.maxResponseBytes)
            response.destroy(new OtlpExportError('OTLP response exceeds configured limit'))
          else chunks.push(chunk)
        })
        response.on('error', fail)
        response.on('end', () => {
          if (response.statusCode < 200 || response.statusCode >= 300) {
            const retryable = [429, 502, 503, 504].includes(response.statusCode)
            return finish(reject, new OtlpExportError(`OTLP HTTP status ${response.statusCode}`, retryable,
              retryable ? retryAfterMilliseconds(response.headers['retry-after']) : null))
          }
          if (!bytes) return finish(resolve, 0)
          if (!(response.headers['content-type'] || '').toLowerCase().startsWith('application/json'))
            return finish(reject, new OtlpExportError('OTLP response content type is not application/json'))
          try {
            const result = JSON.parse(Buffer.concat(chunks).toString('utf8'))
            const partial = result.partialSuccess || {}
            const rejected = Number(partial.rejectedSpans || partial.rejectedDataPoints || 0)
            return finish(resolve, Number.isSafeInteger(rejected) && rejected >= 0 ? rejected : 0)
          } catch { return finish(reject, new OtlpExportError('OTLP response is not valid JSON')) }
        })
      })
      this.activeRequests.add(request)
      request.on('close', () => this.activeRequests.delete(request))
      request.on('error', fail)
      request.end(item.body)
    })
  }
  close() {
    this.closed = true; this.queue.length = 0; this.queuedBytes = 0
    this.stats.queueDepth = 0; this.stats.queueBytes = 0
    for (const cancel of [...this.retryWaiters]) cancel()
    for (const request of this.activeRequests) request.destroy(new Error('OTLP exporter closed'))
  }
}
