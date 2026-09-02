import { useCallback, useEffect, useState } from 'react'
import { bearerHeaders } from '../auth'

function displayTime(milliseconds) {
  return Number.isFinite(milliseconds) ? new Date(milliseconds).toLocaleString() : '—'
}

export function HistoryPanel({ observationToken, backend }) {
  const [records, setRecords] = useState([])
  const [nextCursor, setNextCursor] = useState(null)
  const [message, setMessage] = useState('Loading durable history…')

  const load = useCallback(async (cursor = null, append = false) => {
    try {
      const target = new URL('/api/history', window.location.origin)
      target.searchParams.set('limit', '100')
      if (cursor) target.searchParams.set('cursor', String(cursor))
      const response = await fetch(target, { headers: bearerHeaders(observationToken) })
      const body = await response.json()
      if (!response.ok) throw new Error(body.error || `History returned ${response.status}`)
      setRecords(previous => append ? [...previous, ...body.records] : body.records)
      setNextCursor(body.nextCursor)
      setMessage(body.records.length ? '' : 'No durable records have been retained yet.')
    } catch (error) {
      setMessage(error.message)
      if (!append) setRecords([])
    }
  }, [observationToken])

  useEffect(() => {
    load()
    const timer = setInterval(() => load(), 5000)
    return () => clearInterval(timer)
  }, [load])

  return <section className="history-panel">
    <div className="history-heading"><div><span>DURABLE TELEMETRY</span><h2>Event history</h2></div>
      <div className={`history-state ${backend?.status || 'disabled'}`}>
        {backend?.enabled ? `${backend.backend} · ${backend.status}` : 'history disabled'}
      </div></div>
    <div className="history-stats">
      <span><b>{backend?.written?.toLocaleString?.() || 0}</b> written</span>
      <span><b>{backend?.dropped?.toLocaleString?.() || 0}</b> dropped</span>
      <span><b>{backend?.queueDepth?.toLocaleString?.() || 0}</b> queued</span>
      <span><b>{backend?.databaseBytes?.toLocaleString?.() || 0}</b> database bytes</span>
    </div>
    {message && <div className="history-message">{message}</div>}
    {records.length > 0 && <div className="history-table" role="table">
      <div className="history-row history-header" role="row"><span>Recorded</span><span>Kind</span><span>Event</span><span>Node</span><span>Edge</span><span>Details</span></div>
      {records.map(record => <div className="history-row" role="row" key={record.id}>
        <span>{displayTime(Date.parse(record.recordedAt))}</span><span>{record.kind}</span><span>{record.event || '—'}</span>
        <span>{record.nodeId || '—'}</span><span>{record.edgeId || '—'}</span>
        <span title={JSON.stringify(record.data)}>{JSON.stringify(record.data)}</span>
      </div>)}
    </div>}
    {nextCursor && <button className="history-more" onClick={() => load(nextCursor, true)}>Load older records</button>}
  </section>
}
