import { useEffect, useMemo, useState } from 'react'
import { Boxes, CirclePause, CirclePlay, GitBranch, KeyRound, Network, RotateCcw, TriangleAlert } from 'lucide-react'
import { EdgeInspector } from './components/EdgeInspector'
import { Topology } from './components/Topology'
import { applicationEdges, applicationNodes, edgePaths, infrastructureNodes, networkEdges } from './data/topology'
import { useTelemetry } from './useTelemetry'

function formatBytes(value) {
  if (!Number.isFinite(value)) return '—'
  if (value < 1024) return `${value} B`
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KiB`
  return `${(value / (1024 * 1024)).toFixed(1)} MiB`
}

function formatLatency(value) {
  return Number.isFinite(value) ? `${value.toLocaleString()} µs` : '—'
}

export default function App() {
  const { snapshot, connected } = useTelemetry()
  const [selectedId, setSelectedId] = useState('samples')
  const [controlStatus, setControlStatus] = useState('Enter the control token to pause the source')
  const [controlToken, setControlToken] = useState('')
  const [view, setView] = useState('application')
  const topology = snapshot?.topology
  const graphNodes = useMemo(() => applicationNodes(topology).map(node => {
    const runtime = snapshot?.nodes?.[node.id]
    return { ...node, data: { ...node.data,
      status: runtime?.status || (connected ? 'starting' : 'unavailable'),
      cpu: Number.isFinite(runtime?.cpuPercent) ? runtime.cpuPercent : null,
    }}
  }), [topology, snapshot, connected])
  const edges = useMemo(() => applicationEdges(topology).map(edge => {
    const metric = snapshot?.edges?.[edge.id]
    if (!metric) return edge
    const availableCaptures = snapshot?.capture?.files || []
    const captureFiles = [
      ...availableCaptures.filter(file => file.nodeId === edge.source || file.nodeId === edge.target),
      ...availableCaptures.filter(file => file.format === 'ethernet' &&
        file.nodeId !== edge.source && file.nodeId !== edge.target),
    ]
    return { ...edge, data: { ...edge.data,
      rate: `${metric.messageRate.toLocaleString()} msg/s`,
      byteRate: `${formatBytes(metric.byteRate)}/s`,
      messages: `${metric.sent.toLocaleString()} / ${metric.received.toLocaleString()}`,
      latency: formatLatency(metric.meanLatencyUs), p95Latency: formatLatency(metric.p95LatencyUs),
      drops: metric.drops, errors: metric.errors,
      connection: metric.connection, reconnects: metric.reconnects,
      backpressure: `${metric.backpressureEvents} / ${metric.backpressureUs} µs`,
      rejected: metric.rejected,
      bytes: `${formatBytes(metric.sentWireBytes)} / ${formatBytes(metric.receivedWireBytes)}`,
      metricSources: metric.metricSources,
      captureFiles,
      recent: snapshot.recent?.filter(item => item.edgeId === edge.id) || [],
    }}
  }), [topology, snapshot])
  useEffect(() => {
    if (edges.length && !edges.some(edge => edge.id === selectedId)) setSelectedId(edges[0].id)
  }, [edges, selectedId])
  const selected = useMemo(() => edges.find(e => e.id === selectedId), [edges, selectedId])
  const traffic = useMemo(() => {
    const metrics = Object.values(snapshot?.edges || {})
    const samples = metrics.length ? Math.min(...metrics.map(metric => metric.received || 0)) : 0
    const nodesRunning = graphNodes.length > 0 && graphNodes.every(node => node.data.status === 'running')
    const edgesConnected = metrics.length > 0 && metrics.every(metric => metric.connection === 'connected')
    return { samples, flowing: connected && nodesRunning && edgesConnected && samples > 0 }
  }, [snapshot, graphNodes, connected])
  const pathNodes = useMemo(() => infrastructureNodes(topology).map(node => ({ ...node, data: {
    ...node.data, status: snapshot?.nodes?.[node.id]?.status || node.data.status,
    cpu: Number.isFinite(snapshot?.nodes?.[node.id]?.cpuPercent)
      ? snapshot.nodes[node.id].cpuPercent : null,
  }})), [topology, snapshot])
  const paths = topology?.edgePaths || edgePaths
  const runtimeControl = snapshot?.control || { available: false, connectedNodes: 0 }
  const displayedNodes = view === 'application' ? graphNodes : pathNodes
  const displayedEdges = useMemo(() => view === 'application' ? edges : networkEdges(selectedId, topology), [view, edges, selectedId, topology])

  const control = async (action) => {
    try {
      const response = await fetch(`/api/control/${action}`, { method: 'POST',
        headers: action === 'reset' || !controlToken ? {} : { Authorization: `Bearer ${controlToken}` } })
      const result = await response.json()
      setControlStatus(result.accepted
        ? result.delivered == null ? `${action} accepted by collector`
          : `${action} delivered to ${result.delivered} node${result.delivered === 1 ? '' : 's'}`
        : result.error)
      return result.accepted
    } catch { setControlStatus('Telemetry control service is unavailable'); return false }
  }
  const reset = async () => { await control('reset') }

  return <main>
    <header><div className="brand"><div className="mark"><GitBranch/></div><div><h1>GraphX</h1><p>Development Console</p></div></div>
      <div className="environment"><span className={`live-dot ${connected ? '' : 'offline'}`}/>{connected ? 'LIVE' : 'CONNECTING'} · GRAPHX <span>{snapshot?.graph || 'loading'}</span></div>
    </header>
    <section className="toolbar"><div><div className="breadcrumb"><Boxes size={15}/> Docker host / <strong>graphx</strong></div><h2>Live topology</h2><p>Container boundaries and framed transport telemetry</p></div>
      <div className="toolbar-actions"><button className={view === 'application' ? 'active' : ''} onClick={() => setView('application')}><GitBranch/> Application</button><button className={view === 'network' ? 'active' : ''} onClick={() => setView('network')}><Network/> Network path</button><label className="token-field" title="GRAPHX_CONTROL_TOKEN configured on the telemetry service"><KeyRound/><input aria-label="Control token" type="password" value={controlToken} onChange={event => setControlToken(event.target.value)} placeholder="Control token"/></label><button onClick={() => control('pause')} disabled={!runtimeControl.available || runtimeControl.connectedNodes < 1 || !controlToken || snapshot?.state?.paused} title={runtimeControl.available ? `${runtimeControl.connectedNodes} runtime nodes connected` : 'Set GRAPHX_CONTROL_TOKEN on telemetry'}><CirclePause/> Pause source</button><button onClick={() => control('resume')} disabled={!runtimeControl.available || runtimeControl.connectedNodes < 1 || !controlToken} title="Resume is always available to recover a source after collector restart"><CirclePlay/> Resume</button><button className="danger" disabled title="Use the native Linux netem hooks in the network laboratories"><TriangleAlert/> Fault unavailable</button><button onClick={reset}><RotateCcw/> Reset counters</button></div>
    </section>
    <section className="summary"><span><b>{graphNodes.length}</b> nodes</span><span><b>{edges.length}</b> logical edges</span><span><b>{Object.values(snapshot?.nodes || {}).filter(node => node.status !== 'running').length}</b> starting/offline</span><span className={traffic.flowing ? 'healthy' : 'waiting'}>● {traffic.flowing ? `Traffic flowing · ${traffic.samples.toLocaleString()} samples` : connected ? 'Waiting for samples' : 'Telemetry reconnecting'}</span><span>{controlStatus}</span></section>
    <section className="workspace"><div className="graph-panel"><div className="panel-label"><span>{view === 'application' ? 'APPLICATION DATAFLOW' : 'CONFIGURED NETWORK PATH'}</span><span>Drag nodes · Click edges to inspect</span></div><Topology nodes={displayedNodes} edges={displayedEdges} onEdgeSelect={setSelectedId}/></div><EdgeInspector edge={selected} networkPath={paths[selectedId]}/></section>
    <footer><span>graphx.yaml</span><span>Telemetry · WebSocket</span><span>Capture · {snapshot?.capture?.enabled ? snapshot.capture.provider : 'disabled'}</span></footer>
  </main>
}
