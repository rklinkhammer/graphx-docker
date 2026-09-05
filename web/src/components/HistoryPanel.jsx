import { useCallback, useEffect, useRef, useState } from 'react'
import { bearerHeaders } from '../auth'
import { isCurrentHistoryResponse, mergeHistoryRecords } from '../history.mjs'

function displayTime(milliseconds) {
  return Number.isFinite(milliseconds) ? new Date(milliseconds).toLocaleString() : '—'
}

export function HistoryPanel({ observationToken, backend, packetBackend, preferPackets = false,
  refreshIntervalMs = 5000 }) {
  const [records, setRecords] = useState([])
  const [packetStats, setPacketStats] = useState(null)
  const [nextCursor, setNextCursor] = useState(null)
  const [message, setMessage] = useState('Loading durable history…')
  const [showingOlder, setShowingOlder] = useState(false)
  const autoRefresh = useRef(true)
  const requestGeneration = useRef(0)

  const load = useCallback(async (cursor = null, append = false) => {
    const generation = ++requestGeneration.current
    try {
      const target = new URL(preferPackets ? '/api/packet-history' : '/api/history', window.location.origin)
      target.searchParams.set('limit', '100')
      if (cursor) target.searchParams.set('cursor', String(cursor))
      const response = await fetch(target, { headers: bearerHeaders(observationToken) })
      const body = await response.json()
      if (!isCurrentHistoryResponse(generation, requestGeneration.current)) return
      if (!response.ok) throw new Error(body.error || `History returned ${response.status}`)
      setRecords(previous => mergeHistoryRecords(previous, body.records, append))
      setNextCursor(body.nextCursor)
      if (body.stats) setPacketStats(body.stats)
      setMessage(body.records.length ? '' : append ? 'No older records remain.' :
        'No durable records have been retained yet.')
    } catch (error) {
      if (!isCurrentHistoryResponse(generation, requestGeneration.current)) return
      setMessage(error.message)
      if (!append) setRecords([])
    }
  }, [observationToken, preferPackets])

  useEffect(() => {
    autoRefresh.current = true
    setShowingOlder(false)
    load()
    return () => { requestGeneration.current++ }
  }, [load])

  useEffect(() => {
    const timer = setInterval(() => { if (autoRefresh.current) load() }, refreshIntervalMs)
    return () => clearInterval(timer)
  }, [load, refreshIntervalMs])

  const loadOlder = () => {
    autoRefresh.current = false
    setShowingOlder(true)
    load(nextCursor, true)
  }

  const returnToNewest = () => {
    autoRefresh.current = true
    setShowingOlder(false)
    load()
  }

  const selectedBackend = preferPackets ? packetBackend : backend
  return <section className="history-panel">
    <div className="history-heading"><div><span>{preferPackets ? 'BOUNDED PACKET HISTORY' : 'DURABLE TELEMETRY'}</span><h2>{preferPackets ? 'Raw TCP/UDP observations' : 'Event history'}</h2></div>
      <div className={`history-state ${selectedBackend?.status || 'disabled'}`}>
        {selectedBackend?.enabled ? `${selectedBackend.backend} · ${selectedBackend.status}` : 'history disabled'}
      </div></div>
    <div className="history-stats">
      <span><b>{(preferPackets ? packetStats?.records : backend?.written)?.toLocaleString?.() || 0}</b> retained</span>
      <span><b>{(preferPackets ? packetStats?.dropped : backend?.dropped)?.toLocaleString?.() || 0}</b> dropped</span>
      <span><b>{preferPackets ? packetStats?.maxRecords?.toLocaleString?.() || 0 : backend?.queueDepth?.toLocaleString?.() || 0}</b> {preferPackets ? 'record limit' : 'queued'}</span>
      <span><b>{(preferPackets ? packetStats?.databaseBytes : backend?.databaseBytes)?.toLocaleString?.() || 0}</b> database bytes</span>
    </div>
    {message && <div className="history-message">{message}</div>}
    {records.length > 0 && <div className="history-table" role="table">
      <div className="history-row history-header" role="row"><span>Recorded</span><span>Kind</span><span>Event</span><span>Node</span><span>Edge</span><span>Details</span></div>
      {records.map(record => <div className="history-row" role="row" key={record.id}>
        <span>{displayTime(preferPackets ? record.capturedAt * 1000 : Date.parse(record.recordedAt))}</span><span>{preferPackets ? 'network_packet' : record.kind}</span><span>{preferPackets ? record.protocol : record.event || '—'}</span>
        <span>{preferPackets ? record.direction : record.nodeId || '—'}</span><span>{record.edgeId || '—'}</span>
        <span title={JSON.stringify(preferPackets ? record : record.data)}>{preferPackets ? `${record.sourceAddress}:${record.sourcePort} → ${record.destinationAddress}:${record.destinationPort} · payload ${record.payloadLength} B · captured ${record.capturedLength} / original ${record.originalLength} B${record.truncated ? ' · truncated' : ''}` : JSON.stringify(record.data)}</span>
      </div>)}
    </div>}
    <div className="history-actions">
      {nextCursor && <button className="history-more" onClick={loadOlder}>Load older records</button>}
      {showingOlder && <button className="history-more" onClick={returnToNewest}>Return to newest</button>}
    </div>
  </section>
}
