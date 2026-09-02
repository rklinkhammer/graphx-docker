import { Worker } from 'node:worker_threads'
import { isAbsolute, resolve } from 'node:path'

const MAX_HISTORY_RECORD_BYTES = 16 * 1024

function configInteger(value, fallback, minimum, maximum, name) {
  if (value == null) return fallback
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < minimum || value > maximum)
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}`)
  return value
}

function environmentInteger(value, fallback, minimum, maximum, name) {
  if (value == null || value === '') return fallback
  if (typeof value !== 'string' || !/^[0-9]+$/.test(value))
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}`)
  const parsed = Number(value)
  if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum)
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}`)
  return parsed
}

function configuredInteger(configValue, environmentValue, fallback, minimum, maximum, name) {
  const configured = configInteger(configValue, fallback, minimum, maximum, name)
  return environmentInteger(environmentValue, configured, minimum, maximum, name)
}

function configBoolean(value, fallback, name) {
  if (value == null) return fallback
  if (typeof value !== 'boolean') throw new Error(`${name} must be a boolean`)
  return value
}

function environmentBoolean(value, fallback, name) {
  if (value == null || value === '') return fallback
  if (typeof value !== 'string') throw new Error(`${name} must be true or false`)
  const normalized = value.toLowerCase()
  if (['1', 'true', 'yes', 'on'].includes(normalized)) return true
  if (['0', 'false', 'no', 'off'].includes(normalized)) return false
  throw new Error(`${name} must be true or false`)
}

export function historyConfig(config = {}, env = process.env, baseDirectory = process.cwd()) {
  const enabled = environmentBoolean(env.GRAPHX_HISTORY_ENABLED,
    configBoolean(config.enabled, false, 'history enabled'), 'GRAPHX_HISTORY_ENABLED')
  if (config.backend != null && typeof config.backend !== 'string')
    throw new Error('history backend must be sqlite')
  if (env.GRAPHX_HISTORY_BACKEND != null && env.GRAPHX_HISTORY_BACKEND !== '' &&
      typeof env.GRAPHX_HISTORY_BACKEND !== 'string')
    throw new Error('GRAPHX_HISTORY_BACKEND must be sqlite')
  const backend = env.GRAPHX_HISTORY_BACKEND || config.backend || 'sqlite'
  if (backend !== 'sqlite') throw new Error('history backend must be sqlite')
  const configuredFile = env.GRAPHX_HISTORY_DATABASE_FILE || config.database_file ||
    '.graphx/history.sqlite'
  if (typeof configuredFile !== 'string' || !configuredFile || configuredFile.length > 1024 ||
      configuredFile.includes('\0')) throw new Error('history database_file must be 1-1024 safe characters')
  const databaseFile = isAbsolute(configuredFile) ? configuredFile : resolve(baseDirectory, configuredFile)
  const result = {
    enabled, backend, databaseFile,
    retentionSeconds: configuredInteger(config.retention_seconds,
      env.GRAPHX_HISTORY_RETENTION_SECONDS, 604800, 60, 31536000, 'history retention_seconds'),
    maxRecords: configuredInteger(config.max_records, env.GRAPHX_HISTORY_MAX_RECORDS,
      100000, 10, 10000000, 'history max_records'),
    maxDatabaseBytes: configuredInteger(config.max_database_bytes,
      env.GRAPHX_HISTORY_MAX_DATABASE_BYTES, 268435456, 1048576, 4294967296,
      'history max_database_bytes'),
    queueCapacity: configuredInteger(config.queue_capacity, env.GRAPHX_HISTORY_QUEUE_CAPACITY,
      4096, 1, 65536, 'history queue_capacity'),
    maxQueueBytes: configuredInteger(config.max_queue_bytes, env.GRAPHX_HISTORY_MAX_QUEUE_BYTES,
      8388608, 65536, 67108864, 'history max_queue_bytes'),
    batchSize: configuredInteger(config.batch_size, env.GRAPHX_HISTORY_BATCH_SIZE,
      100, 1, 1000, 'history batch_size'),
    flushIntervalMs: configuredInteger(config.flush_interval_ms,
      env.GRAPHX_HISTORY_FLUSH_INTERVAL_MS, 250, 10, 60000, 'history flush_interval_ms'),
    queryLimit: configuredInteger(config.query_limit, env.GRAPHX_HISTORY_QUERY_LIMIT,
      200, 1, 1000, 'history query_limit'),
    queryTimeoutMs: configuredInteger(config.query_timeout_ms,
      env.GRAPHX_HISTORY_QUERY_TIMEOUT_MS, 2000, 100, 10000, 'history query_timeout_ms'),
    maxPendingQueries: configuredInteger(config.max_pending_queries,
      env.GRAPHX_HISTORY_MAX_PENDING_QUERIES, 16, 1, 128, 'history max_pending_queries'),
    shutdownTimeoutMs: configuredInteger(config.shutdown_timeout_ms,
      env.GRAPHX_HISTORY_SHUTDOWN_TIMEOUT_MS, 2000, 100, 10000,
      'history shutdown_timeout_ms'),
  }
  if (result.batchSize > result.queueCapacity)
    throw new Error('history batch_size must not exceed queue_capacity')
  return result
}

function boundedText(value, maximum) {
  return typeof value === 'string' && value ? value.slice(0, maximum) : null
}

function finiteNumber(value) {
  return Number.isFinite(value) ? value : null
}

export function telemetryHistoryRecord(event, graphId, recordedAtMs = Date.now()) {
  const data = {
    kind: boundedText(event.kind, 32), event: boundedText(event.event, 32),
    nodeId: boundedText(event.nodeId, 64), edgeId: boundedText(event.edgeId, 64),
    sequence: Number.isSafeInteger(event.sequence) ? event.sequence : null,
    traceId: boundedText(event.traceId, 128), spanId: boundedText(event.spanId, 32),
    messageId: boundedText(event.messageId, 128),
    parentMessageId: boundedText(event.parentMessageId, 128),
    latencyUs: finiteNumber(event.latencyUs), wireBytes: finiteNumber(event.wireBytes),
    cpuPercent: finiteNumber(event.cpuPercent), message: boundedText(event.message, 256),
  }
  return { graphId, recordedAtMs, eventAtMs: finiteNumber(event.timestamp) || recordedAtMs,
    kind: data.kind || 'telemetry', event: data.event || 'unknown', nodeId: data.nodeId,
    edgeId: data.edgeId, json: JSON.stringify(data) }
}

export function sloHistoryRecord(slo, readiness, graphId, recordedAtMs = Date.now()) {
  const data = { status: slo.status, met: Boolean(slo.met), observedSeconds: slo.observedSeconds,
    sampleCount: slo.sampleCount, traffic: slo.traffic, objectives: slo.objectives,
    graphReady: Boolean(readiness.ready) }
  return { graphId, recordedAtMs, eventAtMs: recordedAtMs, kind: 'slo',
    event: 'evaluation', nodeId: null, edgeId: null, json: JSON.stringify(data) }
}

export function parseHistoryQuery(searchParams, config, nodeIds = new Set(), edgeIds = new Set()) {
  const allowed = new Set(['cursor', 'after', 'before', 'limit', 'node', 'edge', 'kind', 'event'])
  for (const key of searchParams.keys()) if (!allowed.has(key)) throw new Error(`unknown history query parameter '${key}'`)
  const integer = (name, minimum = 0) => {
    const value = searchParams.get(name)
    if (value == null) return null
    if (!/^[0-9]{1,16}$/.test(value)) throw new Error(`${name} must be a non-negative integer`)
    const parsed = Number(value)
    if (!Number.isSafeInteger(parsed) || parsed < minimum) throw new Error(`${name} is out of range`)
    return parsed
  }
  const identifier = name => {
    const value = searchParams.get(name)
    if (value == null) return null
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(value)) throw new Error(`${name} is invalid`)
    return value
  }
  const limit = integer('limit', 1) ?? config.queryLimit
  if (limit > config.queryLimit) throw new Error(`limit must not exceed ${config.queryLimit}`)
  const node = identifier('node')
  const edge = identifier('edge')
  if (node && nodeIds.size && !nodeIds.has(node)) throw new Error('node is not in the configured topology')
  if (edge && edgeIds.size && !edgeIds.has(edge)) throw new Error('edge is not in the configured topology')
  const kind = identifier('kind')
  const event = identifier('event')
  const after = integer('after')
  const before = integer('before')
  if (after != null && before != null && after > before) throw new Error('after must not exceed before')
  return { cursor: integer('cursor', 1), after, before, limit, node, edge, kind, event }
}

export class HistoryStore {
  constructor(config, graphId) {
    this.config = config
    this.graphId = graphId
    this.queue = []
    this.queueBytes = 0
    this.inFlight = false
    this.inFlightCount = 0
    this.workerExited = false
    this.closing = false
    this.requestId = 0
    this.pending = new Map()
    this.stats = { enabled: config.enabled, backend: config.backend, status: config.enabled ? 'starting' : 'disabled',
      queueDepth: 0, queueBytes: 0, written: 0, failed: 0, dropped: 0, pruned: 0,
      databaseBytes: 0, schemaVersion: null, lastWriteAt: null, lastError: null }
    if (!config.enabled) return
    this.worker = new Worker(new URL('./history-worker.mjs', import.meta.url),
      { workerData: { ...config, graphId } })
    this.worker.on('message', message => this.#onMessage(message))
    this.worker.on('error', error => this.#degrade(error))
    this.worker.on('exit', code => {
      this.workerExited = true
      if (!this.closing && code !== 0 && this.stats.status !== 'degraded')
        this.#degrade(new Error(`history worker exited with code ${code}`))
    })
    this.timer = setInterval(() => this.#drain(), config.flushIntervalMs)
    this.timer.unref()
  }

  enqueue(record) {
    if (!this.config.enabled || this.closing) return false
    if (this.stats.status === 'degraded' || this.stats.status === 'closed') {
      this.stats.dropped++
      return false
    }
    const bytes = Buffer.byteLength(record.json) + 256
    if (bytes > MAX_HISTORY_RECORD_BYTES || this.queue.length >= this.config.queueCapacity ||
        this.queueBytes + bytes > this.config.maxQueueBytes) {
      this.stats.dropped++
      return false
    }
    this.queue.push({ ...record, bytes })
    this.queueBytes += bytes
    this.#queueStats()
    if (this.queue.length >= this.config.batchSize) this.#drain()
    return true
  }

  async waitUntilReady(timeoutMs = this.config.queryTimeoutMs) {
    if (!this.config.enabled) return false
    const deadline = Date.now() + timeoutMs
    while (Date.now() < deadline) {
      if (this.stats.status === 'ready') return true
      if (this.stats.status === 'degraded' || this.stats.status === 'closed')
        throw new Error(this.stats.lastError || `history backend is ${this.stats.status}`)
      await new Promise(resolveWait => setTimeout(resolveWait, 10))
    }
    throw new Error('history backend readiness deadline exceeded')
  }

  async query(query) {
    if (!this.config.enabled) throw new Error('history is disabled')
    if (this.stats.status !== 'ready') throw new Error(`history backend is ${this.stats.status}`)
    if (this.pending.size >= this.config.maxPendingQueries) throw new Error('history query capacity exceeded')
    return this.#request('query', { query }, this.config.queryTimeoutMs)
  }

  async flush(timeoutMs = this.config.shutdownTimeoutMs) {
    if (!this.config.enabled) return
    await this.waitUntilReady(Math.min(timeoutMs, this.config.queryTimeoutMs))
    const deadline = Date.now() + timeoutMs
    this.#drain()
    while ((this.queue.length || this.inFlight) && Date.now() < deadline) {
      await new Promise(resolveWait => setTimeout(resolveWait, 10))
      this.#drain()
    }
    if (this.queue.length || this.inFlight) throw new Error('history flush deadline exceeded')
  }

  async close(timeoutMs = this.config.shutdownTimeoutMs) {
    if (!this.config.enabled || !this.worker || this.stats.status === 'closed') return
    this.closing = true
    clearInterval(this.timer)
    if (this.stats.status === 'degraded' && this.workerExited) {
      await this.worker.terminate()
      this.stats.status = 'closed'
      return
    }
    let closeError = null
    const deadline = Date.now() + timeoutMs
    try {
      while ((this.queue.length || this.inFlight) && Date.now() < deadline) {
        this.#drain(true)
        await new Promise(resolveWait => setTimeout(resolveWait, 10))
      }
      if (this.queue.length) {
        this.stats.failed += this.queue.length
        this.queue.length = 0; this.queueBytes = 0; this.#queueStats()
        closeError = new Error('history shutdown discarded records after the flush deadline')
      }
      if (this.inFlight)
        closeError ||= new Error('history shutdown has an in-flight write after the flush deadline')
      if (closeError) throw closeError
      const remaining = deadline - Date.now()
      if (remaining <= 0) throw new Error('history close deadline exceeded')
      await this.#request('close', {}, remaining)
    } catch (error) {
      this.stats.lastError = String(error?.message || error).slice(0, 256)
      closeError ||= error
      await this.#terminateBy(deadline)
    }
    this.stats.status = 'closed'
    if (closeError) throw closeError
  }

  #queueStats() {
    this.stats.queueDepth = this.queue.length
    this.stats.queueBytes = this.queueBytes
  }

  #drain(allowClosing = false) {
    if (!this.worker || this.inFlight || !this.queue.length || this.stats.status !== 'ready' ||
        (this.closing && !allowClosing)) return
    const records = this.queue.splice(0, this.config.batchSize)
    this.queueBytes -= records.reduce((sum, record) => sum + record.bytes, 0)
    this.#queueStats()
    this.inFlight = true
    this.inFlightCount = records.length
    this.worker.postMessage({ type: 'batch', records: records.map(({ bytes: _bytes, ...record }) => record) })
  }

  #request(type, data, timeoutMs) {
    const requestId = ++this.requestId
    return new Promise((resolveRequest, rejectRequest) => {
      const timer = setTimeout(() => {
        this.pending.delete(requestId)
        rejectRequest(new Error(`history ${type} deadline exceeded`))
      }, timeoutMs)
      this.pending.set(requestId, { resolve: resolveRequest, reject: rejectRequest, timer })
      this.worker.postMessage({ type, requestId, ...data })
    })
  }

  async #terminateBy(deadline) {
    const termination = this.worker.terminate()
    const remaining = deadline - Date.now()
    if (remaining <= 0) return
    await Promise.race([termination, new Promise(resolveWait => {
      const timer = setTimeout(resolveWait, remaining)
      timer.unref()
    })])
  }

  #onMessage(message) {
    if (message.type === 'fatal') {
      this.#degrade(new Error(message.error || 'history worker initialization failed'))
      return
    }
    if (message.type === 'ready') {
      Object.assign(this.stats, { status: 'ready', schemaVersion: message.schemaVersion,
        databaseBytes: message.databaseBytes, lastError: null })
      this.#drain()
      return
    }
    if (message.type === 'batch') {
      this.inFlight = false
      this.inFlightCount = 0
      this.stats.written += message.written || 0
      this.stats.failed += message.failed || 0
      this.stats.pruned += message.pruned || 0
      this.stats.databaseBytes = message.databaseBytes || this.stats.databaseBytes
      this.stats.lastWriteAt = message.written ? new Date().toISOString() : this.stats.lastWriteAt
      this.stats.lastError = message.error || null
      if (message.error) {
        this.#degrade(new Error(message.error))
        return
      }
      this.#drain(this.closing)
      return
    }
    if (message.requestId) {
      const pending = this.pending.get(message.requestId)
      if (!pending) return
      clearTimeout(pending.timer); this.pending.delete(message.requestId)
      if (message.error) pending.reject(new Error(message.error))
      else pending.resolve(message.result)
    }
  }

  #degrade(error) {
    this.stats.status = 'degraded'
    this.stats.lastError = String(error?.message || error).slice(0, 256)
    if (this.inFlightCount) this.stats.failed += this.inFlightCount
    this.inFlight = false
    this.inFlightCount = 0
    if (this.queue.length) {
      this.stats.failed += this.queue.length
      this.queue.length = 0
      this.queueBytes = 0
      this.#queueStats()
    }
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer); pending.reject(new Error(this.stats.lastError))
    }
    this.pending.clear()
  }
}
