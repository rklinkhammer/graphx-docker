export function edgeObservationAvailable(definition, metric) {
  return definition.dataPlane !== 'external' || Number.isFinite(metric?.lastSeen)
}

export function applicationNodes(topology) {
  if (!topology?.nodes?.length) return []
  return topology.nodes.map(node => ({ id: node.id, data: {
    label: node.label, role: node.role, status: 'starting', cpu: null,
    image: node.image, runtime: node.runtime, execution: node.execution,
    lifecycle: node.lifecycle, control: node.control, accelerator: node.accelerator,
    requestedAccelerator: node.requestedAccelerator, selectedAccelerator: node.selectedAccelerator,
    actualAccelerator: node.actualAccelerator, acceleratorEvidence: node.acceleratorEvidence,
    vmState: node.vmState, guestState: node.guestState, guestProtocols: node.guestProtocols,
    guestArchitecture: node.guestArchitecture, input: node.input, output: node.output,
  }}))
}

export function applicationEdges(topology) {
  if (!topology?.edges?.length) return []
  return topology.edges.map(edge => ({ id: edge.id, source: edge.source, target: edge.target,
    type: 'telemetry', data: { label: edge.id, rate: '—', byteRate: '—', messages: '—', latency: '—',
      p95Latency: '—', bytes: '—', errors: '—', drops: '—', rejected: '—',
      connection: 'unavailable', reconnects: '—', backpressure: '—',
      port: edge.port, schema: edge.schema, transport: edge.transport,
      framing: edge.framing, dataPlane: edge.dataPlane,
      observationSource: edge.observationSource, diagnosticState: edge.diagnosticState,
      diagnosticEvidence: edge.diagnosticEvidence, diagnosticLayer: edge.diagnosticLayer } }))
}

export function infrastructureNodes(topology) {
  if (!topology?.networkNodes?.length) return []
  return topology.networkNodes.map(node => ({ id: node.id, data: {
    label: node.label, role: node.role, status: node.status || 'modeled', cpu: null,
    image: node.image, runtime: node.runtime, execution: node.execution,
    lifecycle: node.lifecycle, control: node.control, accelerator: node.accelerator,
    requestedAccelerator: node.requestedAccelerator, selectedAccelerator: node.selectedAccelerator,
    actualAccelerator: node.actualAccelerator, acceleratorEvidence: node.acceleratorEvidence,
    hierarchy: node.hierarchy, parent: node.parent, runtimeLayer: node.runtimeLayer,
    vmState: node.vmState, guestState: node.guestState, guestProtocols: node.guestProtocols,
    guestArchitecture: node.guestArchitecture, input: node.input, output: node.output,
  }}))
}

export function networkEdges(selectedId, topology) {
  const edges = []
  const paths = topology?.edgePaths || {}
  for (const [logicalEdge, path] of Object.entries(paths)) {
    const definition = topology?.edges?.find(edge => edge.id === logicalEdge)
    path.slice(0, -1).forEach((source, index) => edges.push({
      id: `${logicalEdge}-hop-${index}`, source, target: path[index + 1], type: 'telemetry',
      data: { logicalEdge, highlighted: logicalEdge === selectedId,
        diagnosticState: definition?.diagnosticState,
        diagnosticEvidence: definition?.diagnosticEvidence,
        diagnosticLayer: definition?.diagnosticLayer,
        rate: '—', latency: index === 3 ? 'router' : 'L2' },
    }))
  }
  return edges
}
