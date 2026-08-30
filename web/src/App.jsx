import { useEffect, useMemo, useState } from 'react'
import { Boxes, CirclePause, GitBranch, Network, RotateCcw, TriangleAlert } from 'lucide-react'
import { EdgeInspector } from './components/EdgeInspector'
import { Topology } from './components/Topology'
import { applicationEdges, applicationNodes, edgePaths, infrastructureNodes, networkEdges } from './data/topology'
import { useTelemetry } from './useTelemetry'

export default function App() {
  const { snapshot, connected } = useTelemetry()
  const [selectedId, setSelectedId] = useState('samples')
  const [paused, setPaused] = useState(false)
  const [faulted, setFaulted] = useState(false)
  const [controlStatus, setControlStatus] = useState('Runtime controls are not connected')
  const [view, setView] = useState('application')
  const topology = snapshot?.topology
  const graphNodes = useMemo(() => applicationNodes(topology).map(node => ({ ...node, data: { ...node.data,
    status: snapshot?.nodes?.[node.id]?.status || (connected ? 'starting' : 'unavailable'),
  }})), [topology, snapshot, connected])
  const edges = useMemo(() => applicationEdges(topology).map(edge => {
    const metric = snapshot?.edges?.[edge.id]
    if (!metric) return edge
    return { ...edge, data: { ...edge.data,
      rate: `${metric.rate} msg/s`, messages: metric.messages.toLocaleString(),
      latency: `${metric.latencyUs} µs`, drops: metric.drops,
      connection: metric.connection, reconnects: metric.reconnects,
      backpressure: `${metric.backpressureEvents} / ${metric.backpressureUs} µs`,
      bytes: metric.wireBytes, recent: snapshot.recent?.filter(item => item.edgeId === edge.id) || [],
    }}
  }), [topology, snapshot])
  useEffect(() => {
    if (edges.length && !edges.some(edge => edge.id === selectedId)) setSelectedId(edges[0].id)
  }, [edges, selectedId])
  const selected = useMemo(() => edges.find(e => e.id === selectedId), [edges, selectedId])
  const pathNodes = useMemo(() => infrastructureNodes(topology).map(node => ({ ...node, data: {
    ...node.data, status: snapshot?.nodes?.[node.id]?.status || node.data.status,
  }})), [topology, snapshot])
  const paths = topology?.edgePaths || edgePaths
  const displayedNodes = view === 'application' ? graphNodes : pathNodes
  const displayedEdges = useMemo(() => view === 'application' ? edges : networkEdges(selectedId, topology), [view, edges, selectedId, topology])

  const control = async (action) => {
    try {
      const response = await fetch(`/api/control/${action}`, { method: 'POST' })
      const result = await response.json()
      setControlStatus(result.accepted ? `${action} accepted` : result.error)
      if (result.accepted && action === 'pause') setPaused(true)
      if (result.accepted && action === 'resume') setPaused(false)
      if (result.accepted && action === 'fault') setFaulted(value => !value)
      return result.accepted
    } catch { setControlStatus('Telemetry control service is unavailable'); return false }
  }
  const reset = async () => { if (await control('reset')) { setFaulted(false); setPaused(false) } }

  return <main>
    <header><div className="brand"><div className="mark"><GitBranch/></div><div><h1>GraphX</h1><p>Development Console</p></div></div>
      <div className="environment"><span className={`live-dot ${connected ? '' : 'offline'}`}/>{connected ? 'LIVE' : 'CONNECTING'} · GRAPHX <span>{snapshot?.graph || 'loading'}</span></div>
    </header>
    <section className="toolbar"><div><div className="breadcrumb"><Boxes size={15}/> Docker host / <strong>graphx</strong></div><h2>Live topology</h2><p>Container boundaries and framed transport telemetry</p></div>
      <div className="toolbar-actions"><button className={view === 'application' ? 'active' : ''} onClick={() => setView('application')}><GitBranch/> Application</button><button className={view === 'network' ? 'active' : ''} onClick={() => setView('network')}><Network/> Network path</button><button className={paused ? 'active' : ''} onClick={() => control(paused ? 'resume' : 'pause')}><CirclePause/> {paused ? 'Resume' : 'Pause'}</button><button className={faulted ? 'danger active' : 'danger'} onClick={() => control('fault')}><TriangleAlert/> {faulted ? 'Fault active' : 'Inject fault'}</button><button onClick={reset}><RotateCcw/> Reset</button></div>
    </section>
    <section className="summary"><span><b>{graphNodes.length}</b> nodes</span><span><b>{edges.length}</b> logical edges</span><span><b>{Object.values(snapshot?.nodes || {}).filter(node => node.status !== 'running').length}</b> starting/offline</span><span className="healthy">● {connected ? 'Telemetry connected' : 'Metrics unavailable'}</span><span>{controlStatus}</span></section>
    <section className="workspace"><div className="graph-panel"><div className="panel-label"><span>{view === 'application' ? 'APPLICATION DATAFLOW' : 'CONFIGURED NETWORK PATH'}</span><span>Drag nodes · Click edges to inspect</span></div><Topology nodes={displayedNodes} edges={displayedEdges} onEdgeSelect={setSelectedId}/></div><EdgeInspector edge={selected} networkPath={paths[selectedId]}/></section>
    <footer><span>graphx.yaml</span><span>Telemetry · WebSocket</span><span>Capture provider · integration stub</span></footer>
  </main>
}
