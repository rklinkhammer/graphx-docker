import { mkdirSync, statSync } from 'node:fs'
import { dirname } from 'node:path'
import { parentPort, workerData } from 'node:worker_threads'
import { DatabaseSync } from 'node:sqlite'

const SCHEMA_VERSION = 1
let database
let insertRecord
let maintenanceTimer

function databaseBytes() {
  let bytes = 0
  for (const suffix of ['', '-wal', '-shm']) {
    try { bytes += statSync(`${workerData.databaseFile}${suffix}`).size } catch { /* absent */ }
  }
  return bytes
}

function migrate() {
  mkdirSync(dirname(workerData.databaseFile), { recursive: true, mode: 0o750 })
  database = new DatabaseSync(workerData.databaseFile)
  database.exec(`PRAGMA busy_timeout=${Math.min(2000, workerData.shutdownTimeoutMs)};
    PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;`)
  const version = Number(database.prepare('PRAGMA user_version').get().user_version)
  if (version > SCHEMA_VERSION)
    throw new Error(`history schema version ${version} is newer than supported version ${SCHEMA_VERSION}`)
  if (version === 0) {
    database.exec('PRAGMA auto_vacuum=INCREMENTAL; VACUUM; BEGIN IMMEDIATE;')
    try {
      database.exec(`
        CREATE TABLE history_metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE history_records (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          graph_id TEXT NOT NULL,
          recorded_at_ms INTEGER NOT NULL,
          event_at_ms INTEGER NOT NULL,
          kind TEXT NOT NULL,
          event TEXT NOT NULL,
          node_id TEXT,
          edge_id TEXT,
          data_json TEXT NOT NULL
        );
        CREATE INDEX history_recorded_at ON history_records(recorded_at_ms, id);
        CREATE INDEX history_node_time ON history_records(node_id, recorded_at_ms, id);
        CREATE INDEX history_edge_time ON history_records(edge_id, recorded_at_ms, id);
        PRAGMA user_version=1;
        COMMIT;
      `)
    } catch (error) { database.exec('ROLLBACK'); throw error }
  }
  database.exec('PRAGMA journal_mode=WAL;')
  const pageSize = Number(database.prepare('PRAGMA page_size').get().page_size)
  const maxPages = Math.max(1, Math.floor((workerData.maxDatabaseBytes - 65536) / pageSize))
  const currentPages = Number(database.prepare('PRAGMA page_count').get().page_count)
  if (currentPages > maxPages)
    throw new Error(`history database uses ${currentPages * pageSize} bytes, exceeding configured ` +
      `main-file limit ${maxPages * pageSize} bytes; increase max_database_bytes or restore a smaller backup`)
  const effectiveMaxPages = Number(database.prepare(`PRAGMA max_page_count=${maxPages}`).get().max_page_count)
  if (effectiveMaxPages > maxPages)
    throw new Error(`history database effective page limit ${effectiveMaxPages * pageSize} bytes exceeds ` +
      `configured main-file limit ${maxPages * pageSize} bytes`)
  database.exec(`PRAGMA journal_size_limit=${Math.min(16777216,
    Math.floor(workerData.maxDatabaseBytes / 4))};`)
  database.prepare('INSERT OR IGNORE INTO history_metadata(key,value) VALUES (?,?)')
    .run('graph_id', workerData.graphId)
  const storedGraph = database.prepare('SELECT value FROM history_metadata WHERE key=?').get('graph_id')?.value
  if (storedGraph !== workerData.graphId)
    throw new Error(`history database belongs to graph '${storedGraph}', not '${workerData.graphId}'`)
  insertRecord = database.prepare(`INSERT INTO history_records
    (graph_id,recorded_at_ms,event_at_ms,kind,event,node_id,edge_id,data_json)
    VALUES (?,?,?,?,?,?,?,?)`)
}

function maintain(now = Date.now()) {
  let pruned = 0
  pruned += Number(database.prepare('DELETE FROM history_records WHERE recorded_at_ms < ?')
    .run(now - workerData.retentionSeconds * 1000).changes)
  const count = Number(database.prepare('SELECT count(*) AS count FROM history_records').get().count)
  if (count > workerData.maxRecords)
    pruned += Number(database.prepare(`DELETE FROM history_records WHERE id IN
      (SELECT id FROM history_records ORDER BY id ASC LIMIT ?)`).run(count - workerData.maxRecords).changes)
  if (pruned) database.exec('PRAGMA wal_checkpoint(TRUNCATE); PRAGMA incremental_vacuum(1024);')
  return pruned
}

function writeBatch(records) {
  let written = 0
  let pruned = maintain()
  database.exec('BEGIN IMMEDIATE')
  try {
    for (const record of records) {
      insertRecord.run(record.graphId, record.recordedAtMs, record.eventAtMs, record.kind,
        record.event, record.nodeId, record.edgeId, record.json)
      written++
    }
    database.exec('COMMIT')
  } catch (error) {
    try { database.exec('ROLLBACK') } catch { /* SQLite may auto-rollback a full transaction. */ }
    return { written: 0, failed: records.length, pruned, databaseBytes: databaseBytes(),
      error: String(error?.message || error).slice(0, 256) }
  }
  pruned += maintain()
  database.exec('PRAGMA wal_checkpoint(TRUNCATE)')
  return { written, failed: 0, pruned, databaseBytes: databaseBytes() }
}

function queryHistory(query) {
  const clauses = ['graph_id = ?']
  const parameters = [workerData.graphId]
  const add = (sql, value) => { if (value != null) { clauses.push(sql); parameters.push(value) } }
  add('recorded_at_ms >= ?', Date.now() - workerData.retentionSeconds * 1000)
  add('id < ?', query.cursor)
  add('recorded_at_ms >= ?', query.after)
  add('recorded_at_ms <= ?', query.before)
  add('node_id = ?', query.node)
  add('edge_id = ?', query.edge)
  add('kind = ?', query.kind)
  if (query.excludeControlAudit) clauses.push("kind <> 'control_audit'")
  add('event = ?', query.event)
  parameters.push(query.limit + 1)
  const rows = database.prepare(`SELECT id,recorded_at_ms,event_at_ms,kind,event,node_id,edge_id,data_json
    FROM history_records WHERE ${clauses.join(' AND ')} ORDER BY id DESC LIMIT ?`).all(...parameters)
  const hasMore = rows.length > query.limit
  const selected = rows.slice(0, query.limit).map(row => ({
    id: Number(row.id), recordedAt: new Date(Number(row.recorded_at_ms)).toISOString(),
    eventAt: new Date(Number(row.event_at_ms)).toISOString(), kind: row.kind, event: row.event,
    nodeId: row.node_id, edgeId: row.edge_id, data: JSON.parse(row.data_json),
  }))
  return { records: selected, nextCursor: hasMore ? selected.at(-1)?.id || null : null, hasMore }
}

function respond(requestId, result, error = null) {
  parentPort.postMessage({ requestId, result, error: error ? String(error?.message || error).slice(0, 256) : null })
}

function runMaintenance() {
  try {
    parentPort.postMessage({ type: 'maintenance', pruned: maintain(),
      databaseBytes: databaseBytes() })
  } catch (error) {
    clearInterval(maintenanceTimer)
    parentPort.postMessage({ type: 'maintenance', error: String(error?.message || error).slice(0, 256),
      databaseBytes: databaseBytes() })
    try { database.close() } catch { /* The failed operation may already have closed SQLite. */ }
    parentPort.close()
  }
}

try {
  migrate()
  const pruned = maintain()
  parentPort.postMessage({ type: 'ready', schemaVersion: SCHEMA_VERSION, pruned,
    databaseBytes: databaseBytes() })
  const intervalMs = workerData.maintenanceIntervalMs || Math.max(1000,
    Math.min(60000, Math.floor(workerData.retentionSeconds * 500)))
  maintenanceTimer = setInterval(runMaintenance, intervalMs)
} catch (error) {
  parentPort.postMessage({ type: 'fatal', error: String(error?.message || error).slice(0, 256) })
  throw error
}

parentPort.on('message', message => {
  if (message.type === 'batch') {
    parentPort.postMessage({ type: 'batch', ...writeBatch(message.records) })
    return
  }
  if (message.type === 'query') {
    try { respond(message.requestId, queryHistory(message.query)) }
    catch (error) { respond(message.requestId, null, error) }
    return
  }
  if (message.type === 'close') {
    try {
      clearInterval(maintenanceTimer)
      maintain(); database.exec('PRAGMA wal_checkpoint(TRUNCATE)'); database.close()
      respond(message.requestId, { closed: true })
      setImmediate(() => process.exit(0))
    } catch (error) { respond(message.requestId, null, error) }
  }
})
