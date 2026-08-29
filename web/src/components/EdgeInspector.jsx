import { Activity, Clock3, Database, Radio, Search, Waves } from 'lucide-react'

export function EdgeInspector({ edge, networkPath }) {
  if (!edge) return <aside className="inspector empty"><Radio size={28}/><h2>Select an edge</h2><p>Choose a live connection to inspect its framing, metrics, and recent messages.</p></aside>
  const d = edge.data
  return <aside className="inspector">
    <div className="eyebrow">EDGE INSPECTOR</div>
    <h2>{edge.source} <span>→</span> {edge.target}</h2>
    <div className="connection"><span className="pulse"/>TCP {d.connection || 'unavailable'}</div>
    <dl className="facts">
      <div><dt>Destination</dt><dd>{edge.target}:{d.port}</dd></div>
      <div><dt>Schema</dt><dd>{d.schema}</dd></div>
      <div><dt>Framing</dt><dd>u32 big-endian</dd></div>
    </dl>
    <div className="metric-grid">
      <div><Waves/><span>Throughput</span><strong>{d.rate}</strong></div>
      <div><Activity/><span>Messages</span><strong>{d.messages}</strong></div>
      <div><Clock3/><span>Mean latency</span><strong>{d.latency}</strong></div>
      <div><Database/><span>Dropped</span><strong>{d.drops}</strong></div>
      <div><Radio/><span>Reconnects</span><strong>{d.reconnects ?? '—'}</strong></div>
      <div><Waves/><span>Backpressure</span><strong>{d.backpressure ?? '—'}</strong></div>
    </div>
    <h3>Recent messages</h3>
    <div className="messages">
      {d.recent?.length ? d.recent.slice(0, 6).map(message => <div key={`${message.nodeId}-${message.sequence}`}><span><Search size={13}/> {message.sequence}</span><span>{message.type}</span><span>{message.latencyUs} µs</span></div>) : <div><span>—</span><span>Waiting for traffic</span><span>—</span></div>}
    </div>
    <div className="placeholder"><strong>Trace + packet correlation</strong><p>Reserved for OpenTelemetry trace IDs and PCAPNG packet offsets.</p></div>
    <h3>Network path</h3>
    <div className="network-path">{networkPath?.map((hop, index) => <span key={hop}>{index > 0 && <i>→</i>}{hop}</span>)}</div>
    <div className="actions"><button>Inspect messages</button><button disabled>Open capture</button></div>
  </aside>
}
