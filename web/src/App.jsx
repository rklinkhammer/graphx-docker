import { NodeConsolePanel } from './components/NodeConsolePanel'
import { useEffect, useMemo, useState } from 'react'
import { Boxes, CirclePause, CirclePlay, Database, Download, GitBranch, KeyRound, Network, RotateCcw, TriangleAlert } from 'lucide-react'
import { CapturePanel } from './components/CapturePanel'
import { EdgeInspector } from './components/EdgeInspector'
import { ControlCommandStatus } from './components/ControlCommandStatus'
import { HistoryPanel } from './components/HistoryPanel'
import { Topology } from './components/Topology'
import { applicationEdges, applicationNodes, edgeObservationAvailable, infrastructureNodes, networkEdges } from './data/topology'
import { controlCommandRequest, persistObservationToken, initializeConsoleSession } from './auth'
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
  const [observationToken, setObservationToken] = useState(() => sessionStorage.getItem('graphx-observation-token') || '')
  const [session, setSession] = useState(null)
  const { snapshot, connected } = useTelemetry(observationToken, session?.authenticated || false)
  const [selectedId, setSelectedId] = useState('')
  const [consoleNode, setConsoleNode] = useState('')
  const [controlStatus, setControlStatus] = useState('Open an authenticated console with graphx example open')
  const [activeCommand, setActiveCommand] = useState(null)
  const [controlToken, setControlToken] = useState('')
  const [view, setView] = useState('application')
  useEffect(() => {
    let active = true
    const refresh = async () => {
      try {
        const result = await initializeConsoleSession({ location: window.location, history: window.history })
        if (!active) return
        setSession(result)
        if (result.authenticated) {
          if (result.handoff) { setObservationToken(''); setControlToken('') }
          setControlStatus(result.control ? 'Console authenticated' : 'Observation session; control is disabled')
        } else if (result.error) setControlStatus(result.error)
      } catch { if (active) setControlStatus('Console login unavailable; use graphx example open') }
    }
    void refresh()
    const timer = setInterval(refresh, 30_000)
    return () => { active = false; clearInterval(timer) }
  }, [])
  const hasControl = Boolean(controlToken || session?.control)

  useEffect(() => {
    persistObservationToken(sessionStorage, observationToken)
  }, [observationToken])
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
    if (!edgeObservationAvailable(edge.data, metric))
      return { ...edge, data: { ...edge.data, captureFiles,
        connection: metric.diagnosticEvidence ? metric.connection : 'unavailable' } }
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
    const nodesRunning = graphNodes.length > 0 && graphNodes.every(node => ['running', 'ready'].includes(node.data.status))
    const edgesConnected = metrics.length > 0 && metrics.every(metric => metric.connection === 'connected')
    return { samples, flowing: connected && nodesRunning && edgesConnected && samples > 0 }
  }, [snapshot, graphNodes, connected])
  const pathNodes = useMemo(() => infrastructureNodes(topology).map(node => ({ ...node, data: {
    ...node.data, status: node.data.runtimeLayer
      ? node.data.status : snapshot?.nodes?.[node.id]?.status || node.data.status,
    cpu: Number.isFinite(snapshot?.nodes?.[node.id]?.cpuPercent)
      ? snapshot.nodes[node.id].cpuPercent : null,
  }})), [topology, snapshot])
  const paths = topology?.edgePaths || {}
  const runtimeControl = snapshot?.control || { available: false, connectedNodes: 0 }
  const displayedNodes = view === 'application' ? graphNodes : pathNodes
  const displayedEdges = useMemo(() => view === 'application' ? edges : networkEdges(selectedId, topology), [view, edges, selectedId, topology])

  const control = async (action) => {
    setActiveCommand(null)
    try {
      const request = controlCommandRequest(action, controlToken)
      const response = await fetch(request.url, request.options)
      const result = await response.json()
      const command = result.command
      setActiveCommand(result.accepted && command ? { ...command, action } : null)
      setControlStatus(result.accepted && command
        ? `${action} ${command.status} · ${command.id.slice(0, 8)} · ${command.delivered} target${command.delivered === 1 ? '' : 's'}`
        : result.accepted ? `${action} accepted by collector` : result.error)
      return result.accepted
    } catch { setControlStatus('Telemetry control service is unavailable'); return false }
  }
  const reset = async () => { await control('reset') }

  return <main>
    <header><div className="brand"><div className="mark"><GitBranch/></div><div><h1>GraphX</h1><p>Development Console</p></div></div>
      <div className="environment"><span className={`live-dot ${connected ? '' : 'offline'}`}/>{connected ? 'LIVE' : 'CONNECTING'} · GRAPHX <span>{snapshot?.graph || 'loading'}</span></div>
    </header>
    <section className="toolbar"><div><div className="breadcrumb"><Boxes size={15}/> Runtime / <strong>{snapshot?.graph || 'graphx'}</strong></div><h2>Live topology</h2><p>Application, container, virtual-machine, and network observations</p></div>
      <div className="toolbar-actions"><button className={view === 'application' ? 'active' : ''} onClick={() => setView('application')}><GitBranch/> Application</button><button className={view === 'network' ? 'active' : ''} onClick={() => setView('network')}><Network/> Network path</button><button className={view === 'history' ? 'active' : ''} onClick={() => setView('history')}><Database/> History</button><button className={view === 'capture' ? 'active' : ''} onClick={() => setView('capture')}><Download/> Capture</button><details className="manual-auth"><summary>Manual authentication</summary><label className="token-field" title="Optional GRAPHX_OBSERVATION_TOKEN"><KeyRound/><input aria-label="Observation token" type="password" value={observationToken} onChange={event => setObservationToken(event.target.value)} placeholder="Observation token"/></label><label className="token-field" title="GRAPHX_CONTROL_TOKEN configured on the telemetry service"><KeyRound/><input aria-label="Control token" type="password" value={controlToken} onChange={event => setControlToken(event.target.value)} placeholder="Control token"/></label></details><button onClick={() => control('pause')} disabled={!runtimeControl.available || runtimeControl.connectedNodes < 1 || !hasControl || snapshot?.state?.paused} title={runtimeControl.available ? `${runtimeControl.connectedNodes} runtime nodes connected` : 'Set GRAPHX_CONTROL_TOKEN on telemetry'}><CirclePause/> Pause source</button><button onClick={() => control('resume')} disabled={!runtimeControl.available || runtimeControl.connectedNodes < 1 || !hasControl} title="Resume is always available to recover a source after collector restart"><CirclePlay/> Resume</button><button className="danger" disabled title="Use the native Linux netem hooks in the network laboratories"><TriangleAlert/> Fault unavailable</button><button onClick={reset} disabled={!hasControl}><RotateCcw/> Reset counters</button></div>
    </section>
    <section className="summary"><span><b>{graphNodes.length}</b> nodes</span><span><b>{edges.length}</b> logical edges</span><span><b>{Object.values(snapshot?.nodes || {}).filter(node => !['running', 'ready'].includes(node.status)).length}</b> not ready</span><span className={traffic.flowing ? 'healthy' : 'waiting'}>● {traffic.flowing ? `Traffic flowing · ${traffic.samples.toLocaleString()} samples` : connected ? 'Waiting for samples' : 'Telemetry reconnecting'}</span><ControlCommandStatus command={activeCommand} token={controlToken} fallback={controlStatus}/></section>
    <div className="main-view">{view === 'history' ? <HistoryPanel observationToken={observationToken} backend={snapshot?.history} packetBackend={snapshot?.packetHistory} preferPackets={edges.some(edge => edge.data.dataPlane === 'external')}/>
      : view === 'capture' ? <CapturePanel capture={snapshot?.capture} observationToken={observationToken}/>
      : <section className="workspace"><div className="graph-panel"><div className="panel-label"><span>{view === 'application' ? 'APPLICATION DATAFLOW' : 'CONFIGURED NETWORK PATH'}</span><span>Click nodes for console · Click edges to inspect</span></div><Topology nodes={displayedNodes} edges={displayedEdges} onNodeSelect={setConsoleNode} onEdgeSelect={setSelectedId}/></div><EdgeInspector edge={selected} networkPath={paths[selectedId]} observationToken={observationToken}/></section>}</div>
    <section className="console-dock" aria-label="Node console">
      <div className="console-dock-heading"><strong>Node console</strong><label>Node <select aria-label="Console node" value={graphNodes.some(node => node.id === consoleNode) ? consoleNode : ''} onChange={event => setConsoleNode(event.target.value)}>
        <option value="">Select a node</option>{graphNodes.map(node => <option key={node.id} value={node.id}>{node.id}</option>)}
      </select></label></div>
      {graphNodes.some(node => node.id === consoleNode)
        ? <NodeConsolePanel key={consoleNode} node={graphNodes.find(node => node.id === consoleNode)} observationToken={observationToken} controlToken={controlToken} hasControl={hasControl}/>
        : <p className="console-empty">Select a node above or click an application node in the topology to view its logs and available serial console.</p>}
    </section>
    <footer><span>graphx.yaml</span><span>Telemetry · WebSocket</span><span>Capture · {snapshot?.capture?.enabled ? snapshot.capture.provider : 'disabled'}</span></footer>
  </main>
}
