import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import dgram from 'node:dgram'
import { once } from 'node:events'
import { mkdtempSync, rmSync, statSync } from 'node:fs'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { DatabaseSync } from 'node:sqlite'
import test from 'node:test'
import { fileURLToPath } from 'node:url'
import { HistoryStore, historyConfig, parseHistoryQuery, telemetryHistoryRecord } from './history.mjs'

function temporaryHistory() {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-history-'))
  return { directory, file: join(directory, 'history.sqlite'),
    remove: () => rmSync(directory, { recursive: true, force: true }) }
}

function config(file, overrides = {}) {
  return historyConfig({ enabled: true, database_file: file, retention_seconds: 3600,
    max_records: 10, max_database_bytes: 1048576, queue_capacity: 16,
    max_queue_bytes: 65536, batch_size: 4, flush_interval_ms: 10,
    query_limit: 20, query_timeout_ms: 2000, max_pending_queries: 4,
    shutdown_timeout_ms: 2000, ...overrides }, {})
}

test('history configuration and query filters are strict and bounded', () => {
  assert.throws(() => historyConfig({ enabled: true, backend: 'unknown' }, {}), /backend/)
  assert.throws(() => historyConfig({ enabled: true, batch_size: 2, queue_capacity: 1 }, {}),
    /batch_size/)
  assert.throws(() => historyConfig({ enabled: 'false' }, {}), /must be a boolean/)
  assert.throws(() => historyConfig({ enabled: false, batch_size: true }, {}), /integer/)
  assert.throws(() => historyConfig({ enabled: false, query_limit: '20' }, {}), /integer/)
  assert.throws(() => historyConfig({ enabled: false }, { GRAPHX_HISTORY_QUERY_LIMIT: '1.5' }),
    /integer/)
  assert.equal(historyConfig({ enabled: false, query_limit: 5 },
    { GRAPHX_HISTORY_ENABLED: 'true', GRAPHX_HISTORY_QUERY_LIMIT: '9' }).enabled, true)
  assert.equal(historyConfig({ enabled: false, query_limit: 5 },
    { GRAPHX_HISTORY_QUERY_LIMIT: '' }).queryLimit, 5)
  const settings = config('/tmp/graphx-history-test.sqlite')
  assert.throws(() => parseHistoryQuery(new URLSearchParams('limit=21'), settings), /limit/)
  assert.throws(() => parseHistoryQuery(new URLSearchParams('unknown=1'), settings), /unknown/)
  assert.throws(() => parseHistoryQuery(new URLSearchParams('after=2&before=1'), settings), /after/)
  assert.throws(() => parseHistoryQuery(new URLSearchParams('node=missing'), settings,
    new Set(['generator'])), /topology/)
  assert.deepEqual(parseHistoryQuery(new URLSearchParams('limit=5&node=generator'), settings,
    new Set(['generator'])), { cursor: null, after: null, before: null, limit: 5,
    node: 'generator', edge: null, kind: null, event: null })
})

test('SQLite history persists across restart and uses stable bounded pagination', async () => {
  const temporary = temporaryHistory()
  const settings = config(temporary.file)
  const now = Date.now()
  let store = new HistoryStore(settings, 'graphx')
  try {
    await store.waitUntilReady()
    for (let index = 0; index < 15; ++index)
      assert.equal(store.enqueue(telemetryHistoryRecord({ kind: 'trace', event: 'send',
        nodeId: 'generator', edgeId: 'samples', sequence: index, timestamp: now + index },
      'graphx', now + index)), true)
    await store.flush()
    assert.equal(store.stats.written, 15)
    assert.ok(store.stats.pruned >= 5)
    const first = await store.query({ cursor: null, after: null, before: null, limit: 4,
      node: null, edge: null, kind: null, event: null })
    assert.equal(first.records.length, 4)
    assert.equal(first.hasMore, true)
    assert.ok(first.nextCursor)
    const second = await store.query({ cursor: first.nextCursor, after: null, before: null,
      limit: 20, node: null, edge: null, kind: null, event: null })
    assert.equal(second.records.length, 6)
    assert.ok(second.records.every(record => record.id < first.nextCursor))
    await store.close()

    store = new HistoryStore(settings, 'graphx')
    await store.waitUntilReady()
    const persisted = await store.query({ cursor: null, after: null, before: null, limit: 20,
      node: null, edge: null, kind: null, event: null })
    assert.equal(persisted.records.length, 10)
    assert.equal(persisted.records[0].data.sequence, 14)
  } finally {
    await store.close().catch(() => {})
    temporary.remove()
  }
})

test('history write queue drops newest records at configured item capacity', async () => {
  const temporary = temporaryHistory()
  const settings = config(temporary.file, { queue_capacity: 1, batch_size: 1,
    flush_interval_ms: 60000 })
  const store = new HistoryStore(settings, 'graphx')
  try {
    const record = telemetryHistoryRecord({ kind: 'trace', event: 'send', timestamp: Date.now() }, 'graphx')
    assert.equal(store.enqueue(record), true)
    assert.equal(store.enqueue(record), false)
    assert.equal(store.stats.dropped, 1)
    assert.ok(store.stats.queueBytes <= settings.maxQueueBytes)
    await store.waitUntilReady()
    await store.flush()
  } finally { await store.close().catch(() => {}); temporary.remove() }
})

test('SQLite write failure degrades history and discards queued work without blocking', async () => {
  const temporary = temporaryHistory()
  const settings = config(temporary.file, { max_records: 1000, queue_capacity: 200,
    max_queue_bytes: 4 * 1024 * 1024, batch_size: 100 })
  const store = new HistoryStore(settings, 'graphx')
  try {
    await store.waitUntilReady()
    const now = Date.now()
    const record = { graphId: 'graphx', recordedAtMs: now, eventAtMs: now, kind: 'telemetry',
      event: 'send', nodeId: 'generator', edgeId: 'samples',
      json: JSON.stringify({ message: 'x'.repeat(14000) }) }
    for (let index = 0; index < 120; ++index) assert.equal(store.enqueue(record), true)
    for (let attempt = 0; attempt < 200 && store.stats.status !== 'degraded'; ++attempt)
      await new Promise(resolveWait => setTimeout(resolveWait, 10))
    assert.equal(store.stats.status, 'degraded')
    assert.match(store.stats.lastError, /full/i)
    assert.ok(store.stats.failed >= 100)
    assert.equal(store.stats.queueDepth, 0)
    assert.equal(store.enqueue(record), false)
  } finally { await store.close().catch(() => {}); temporary.remove() }
})

test('history rejects an existing database above a reduced main-file limit', async () => {
  const temporary = temporaryHistory()
  let store = new HistoryStore(config(temporary.file, { max_database_bytes: 4 * 1024 * 1024,
    max_records: 1000, queue_capacity: 300, max_queue_bytes: 4 * 1024 * 1024,
    batch_size: 100 }), 'graphx')
  try {
    await store.waitUntilReady()
    const now = Date.now()
    for (let index = 0; index < 180; ++index)
      assert.equal(store.enqueue({ graphId: 'graphx', recordedAtMs: now + index,
        eventAtMs: now + index, kind: 'telemetry', event: 'send', nodeId: 'generator',
        edgeId: 'samples', json: JSON.stringify({ sequence: index,
          message: 'x'.repeat(14000) }) }), true)
    await store.flush(); await store.close()
    assert.ok(statSync(temporary.file).size > 1024 * 1024)

    store = new HistoryStore(config(temporary.file, { max_database_bytes: 1024 * 1024,
      max_records: 1000 }), 'graphx')
    await assert.rejects(store.waitUntilReady(), /exceeding configured main-file limit/)
    assert.equal(store.stats.status, 'degraded')
  } finally { await store.close().catch(() => {}); temporary.remove() }
})

test('history shutdown deadline bounds a worker blocked by another SQLite writer', async () => {
  const temporary = temporaryHistory()
  const store = new HistoryStore(config(temporary.file, { batch_size: 1,
    shutdown_timeout_ms: 100 }), 'graphx')
  let lock
  try {
    await store.waitUntilReady()
    lock = new DatabaseSync(temporary.file)
    lock.exec('BEGIN IMMEDIATE')
    assert.equal(store.enqueue(telemetryHistoryRecord({ kind: 'trace', event: 'send',
      nodeId: 'generator', edgeId: 'samples', timestamp: Date.now() }, 'graphx')), true)
    const started = Date.now()
    await assert.rejects(store.close(100), /shutdown|deadline/)
    assert.ok(Date.now() - started < 500, 'close must honor the elapsed shutdown budget')
    assert.equal(store.stats.status, 'closed')
  } finally {
    try { lock?.exec('ROLLBACK') } catch { /* already released */ }
    try { lock?.close() } catch { /* already closed */ }
    await store.close().catch(() => {})
    temporary.remove()
  }
})

test('history rejects newer schemas and databases owned by another graph', async () => {
  const newer = temporaryHistory()
  const database = new DatabaseSync(newer.file)
  database.exec('PRAGMA user_version=2'); database.close()
  const unsupported = new HistoryStore(config(newer.file), 'graphx')
  await assert.rejects(unsupported.waitUntilReady(), /newer than supported/)
  await unsupported.close().catch(() => {})
  newer.remove()

  const owned = temporaryHistory()
  let first = new HistoryStore(config(owned.file), 'first')
  await first.waitUntilReady(); await first.close()
  first = new HistoryStore(config(owned.file), 'second')
  await assert.rejects(first.waitUntilReady(), /belongs to graph 'first'/)
  await first.close().catch(() => {})
  owned.remove()
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

async function startTelemetry(databaseFile) {
  const directory = dirname(fileURLToPath(import.meta.url))
  const port = await availablePort()
  const udpPort = await availablePort()
  const child = spawn(process.execPath, ['server.mjs'], { cwd: directory,
    env: { ...process.env, PORT: String(port), GRAPHX_TELEMETRY_PORT: String(udpPort),
      GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
      GRAPHX_CONFIG: resolve(directory, '../../graphx.yaml'), GRAPHX_HISTORY_ENABLED: 'true',
      GRAPHX_HISTORY_DATABASE_FILE: databaseFile, GRAPHX_HISTORY_FLUSH_INTERVAL_MS: '10',
      GRAPHX_HISTORY_BATCH_SIZE: '1', GRAPHX_OBSERVATION_TOKEN: secret },
    stdio: ['ignore', 'pipe', 'pipe'] })
  const headers = { authorization: `Bearer ${secret}` }
  for (let attempt = 0; attempt < 100; ++attempt) {
    try {
      const status = await fetch(`http://127.0.0.1:${port}/api/history/status`, { headers })
      if (status.ok && ['ready', 'degraded'].includes((await status.json()).status))
        return { child, port, udpPort, headers }
    } catch { /* process is still starting */ }
    await new Promise(resolveWait => setTimeout(resolveWait, 25))
  }
  child.kill('SIGTERM')
  throw new Error('telemetry history did not start before the test deadline')
}

async function stopTelemetry(child, signal = 'SIGTERM') {
  child.kill(signal)
  if (child.exitCode == null) await once(child, 'exit')
}

const secret = '0123456789abcdef0123456789abcdef'

test('authenticated history API survives abrupt restart and degrades independently',
  { timeout: 15000 }, async () => {
    const temporary = temporaryHistory()
    let running
    try {
      running = await startTelemetry(temporary.file)
      assert.equal((await fetch(`http://127.0.0.1:${running.port}/api/history`)).status, 401)
      const socket = dgram.createSocket('udp4')
      const event = Buffer.from(JSON.stringify({ kind: 'trace', event: 'send', nodeId: 'generator',
        edgeId: 'samples', sequence: 77, timestamp: Date.now(), wireBytes: 64 }))
      await new Promise((resolveSend, rejectSend) => socket.send(event, running.udpPort,
        '127.0.0.1', error => error ? rejectSend(error) : resolveSend()))
      socket.close()
      let records = []
      for (let attempt = 0; attempt < 100 && !records.length; ++attempt) {
        const response = await fetch(`http://127.0.0.1:${running.port}/api/history?limit=10`,
          { headers: running.headers })
        assert.equal(response.status, 200)
        records = (await response.json()).records
        if (!records.length) await new Promise(resolveWait => setTimeout(resolveWait, 25))
      }
      assert.equal(records[0].data.sequence, 77)
      await stopTelemetry(running.child, 'SIGKILL')

      running = await startTelemetry(temporary.file)
      const persisted = await (await fetch(
        `http://127.0.0.1:${running.port}/api/history?limit=10&node=generator`,
        { headers: running.headers })).json()
      assert.equal(persisted.records[0].data.sequence, 77)
      await stopTelemetry(running.child)
      running = null

      const incompatible = join(temporary.directory, 'incompatible.sqlite')
      const database = new DatabaseSync(incompatible)
      database.exec('PRAGMA user_version=2'); database.close()
      running = await startTelemetry(incompatible)
      assert.equal((await fetch(`http://127.0.0.1:${running.port}/api/ready`)).status, 200)
      const status = await (await fetch(`http://127.0.0.1:${running.port}/api/history/status`,
        { headers: running.headers })).json()
      assert.equal(status.status, 'degraded')
      assert.match(status.lastError, /newer than supported/)
      assert.equal((await fetch(`http://127.0.0.1:${running.port}/api/history`,
        { headers: running.headers })).status, 503)
    } finally {
      if (running) await stopTelemetry(running.child)
      temporary.remove()
    }
  })
