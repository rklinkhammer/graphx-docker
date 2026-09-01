import { Activity, Clock3, Database, Radio, Search, Waves } from 'lucide-react'

export function EdgeInspector({ edge, networkPath }) {
  if (!edge) return <aside className="inspector empty"><Radio size={28}/><h2>Select an edge</h2><p>Choose a live connection to inspect its framing, metrics, and recent messages.</p></aside>
  const d = edge.data
  return <aside className="inspector">
    <div className="eyebrow">EDGE INSPECTOR</div>
    <h2>{edge.source} <span>→</span> {edge.target}</h2>
    <div className="connection"><span className="pulse"/>{d.transport || 'transport'} {d.connection || 'unavailable'}</div>
    <dl className="facts">
      <div><dt>Destination</dt><dd>{edge.target}:{d.port}</dd></div>
      <div><dt>Schema</dt><dd>{d.schema}</dd></div>
      <div><dt>Framing</dt><dd>u32 big-endian</dd></div>
    </dl>
    <div className="metric-grid">
      <div><Waves/><span>Throughput</span><strong>{d.rate}</strong></div>
      <div><Activity/><span>Sent / received</span><strong>{d.messages}</strong></div>
      <div><Clock3/><span>Mean latency</span><strong>{d.latency}</strong></div>
      <div><Clock3/><span>p95 latency</span><strong>{d.p95Latency ?? '—'}</strong></div>
      <div><Database/><span>Wire bandwidth</span><strong>{d.byteRate ?? '—'}</strong></div>
      <div><Database/><span>Bytes sent / received</span><strong>{d.bytes ?? '—'}</strong></div>
      <div><Activity/><span>Errors / dropped</span><strong>{d.errors ?? '—'} / {d.drops}</strong></div>
      <div><Radio/><span>Reconnects</span><strong>{d.reconnects ?? '—'}</strong></div>
      <div><Waves/><span>Backpressure</span><strong>{d.backpressure ?? '—'}</strong></div>
      <div><Activity/><span>Rejected</span><strong>{d.rejected ?? '—'}</strong></div>
    </div>
    <p className="metric-basis">Counters and latency are measured · rates are derived over 5 s · unavailable values are shown as —</p>
    <h3>Recent messages</h3>
    <div className="messages">
      {d.recent?.length ? d.recent.slice(0, 6).map(message => {
        const capture = message.captures?.[0]
        return <div key={message.messageId || `${message.nodeId}-${message.sequence}-${message.timestamp}`}><span><Search size={13}/> {message.sequence}</span><span>{message.type || 'unknown'}</span><span>{message.latencyUs} µs</span><span title={`Message: ${message.messageId || 'unavailable'}\nTrace: ${message.traceId || 'unavailable'}`}>{message.messageId ? message.messageId.slice(0, 8) : message.traceId ? message.traceId.slice(0, 8) : '—'}</span><span title={capture ? `${capture.captureFile} byte ${capture.captureOffset}` : 'Capture unavailable'}>{capture ? `#${capture.capturePacket}` : '—'}</span></div>
      }) : <div><span>—</span><span>Waiting for traffic</span><span>—</span><span>—</span><span>—</span></div>}
    </div>
    <div className="placeholder"><strong>Identity + capture correlation</strong><p>Message IDs correlate telemetry with exact PCAPNG records; trace IDs group causal work. GraphX frames use LINKTYPE_USER0 and are not labeled as Ethernet packets.</p></div>
    <h3>Network path</h3>
    <div className="network-path">{networkPath?.map((hop, index) => <span key={hop}>{index > 0 && <i>→</i>}{hop}</span>)}</div>
    <div className="actions"><button>Inspect messages</button>{d.captureFiles?.length ? d.captureFiles.slice(0, 2).map(file => <a key={file.name} href={file.url} download title={file.name}>Download {file.format === 'ethernet' ? 'Ethernet' : 'GraphX'}</a>) : <button disabled>Capture unavailable</button>}</div>
  </aside>
}
