import { useMemo, useState } from 'react'
import { Boxes, CirclePause, GitBranch, RotateCcw, TriangleAlert } from 'lucide-react'
import { EdgeInspector } from './components/EdgeInspector'
import { Topology } from './components/Topology'
import { initialEdges, initialNodes } from './data/topology'
import { useTelemetry } from './useTelemetry'

export default function App() {
  const { snapshot, connected } = useTelemetry()
  const [selectedId, setSelectedId] = useState('samples')
  const [paused, setPaused] = useState(false)
  const [faulted, setFaulted] = useState(false)
  const nodes = useMemo(() => initialNodes.map(node => ({ ...node, data: { ...node.data,
    status: snapshot?.nodes?.[node.id]?.status || (connected ? 'starting' : 'preview'),
  }})), [snapshot, connected])
  const edges = useMemo(() => initialEdges.map(edge => {
    const metric = snapshot?.edges?.[edge.id]
    if (!metric) return edge
    return { ...edge, data: { ...edge.data,
      rate: `${metric.rate} msg/s`, messages: metric.messages.toLocaleString(),
      latency: `${metric.latencyUs} µs`, drops: metric.drops,
      bytes: metric.wireBytes, recent: snapshot.recent?.filter(item => item.edgeId === edge.id) || [],
    }}
  }), [snapshot])
  const selected = useMemo(() => edges.find(e => e.id === selectedId), [edges, selectedId])

  const control = (action) => fetch(`/api/control/${action}`, { method: 'POST' }).catch(() => {})
  const reset = () => { setFaulted(false); setPaused(false); control('reset') }

  return <main>
    <header><div className="brand"><div className="mark"><GitBranch/></div><div><h1>GraphX</h1><p>Development Console</p></div></div>
      <div className="environment"><span className={`live-dot ${connected ? '' : 'offline'}`}/>{connected ? 'LIVE' : 'CONNECTING'} · DOCKER <span>sample-pipeline</span></div>
    </header>
    <section className="toolbar"><div><div className="breadcrumb"><Boxes size={15}/> Docker host / <strong>graphx</strong></div><h2>Live topology</h2><p>Container boundaries and framed transport telemetry</p></div>
      <div className="toolbar-actions"><button className={paused ? 'active' : ''} onClick={() => { setPaused(!paused); control(paused ? 'resume' : 'pause') }}><CirclePause/> {paused ? 'Resume' : 'Pause'}</button><button className={faulted ? 'danger active' : 'danger'} onClick={() => { setFaulted(!faulted); control('fault') }}><TriangleAlert/> {faulted ? 'Fault active' : 'Inject fault'}</button><button onClick={reset}><RotateCcw/> Reset</button></div>
    </section>
    <section className="summary"><span><b>3</b> containers</span><span><b>2</b> TCP edges</span><span><b>{Object.values(snapshot?.nodes || {}).filter(node => node.status !== 'running').length}</b> starting/offline</span><span className="healthy">● {connected ? 'Telemetry connected' : 'Preview values'}</span></section>
    <section className="workspace"><div className="graph-panel"><div className="panel-label"><span>BRIDGE NETWORK · graphx</span><span>Drag nodes · Click edges to inspect</span></div><Topology nodes={nodes} edges={edges} onEdgeSelect={setSelectedId}/></div><EdgeInspector edge={selected}/></section>
    <footer><span>graphx.yaml</span><span>Telemetry · WebSocket</span><span>Capture provider · integration stub</span></footer>
  </main>
}
