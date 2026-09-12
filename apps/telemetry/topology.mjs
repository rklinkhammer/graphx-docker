export function createTopology(config, environment = process.env) {
  const graph = config.graph || { id: 'graphx', nodes: [], edges: [] }
  const deployment = config.deployment?.services || {}
  const transport = config.transport || {}
  const requestedQemuAccelerator = environment.GRAPHX_QEMU_REQUESTED_ACCEL || ''
  const selectedQemuAccelerator = environment.GRAPHX_QEMU_ACCEL || ''
  const externalObservationSource = graph.nodes.some(node => node.runtime === 'qemu')
    ? 'qemu-pcap'
    : config.observability?.capture?.provider === 'ovs-span' ? 'ovs-span' : 'ethernet-pcap'
  const graphNodes = graph.nodes.map(node => ({
    id: node.id, label: node.id, role: node.kind, image: deployment[node.id]?.image || 'local process',
    runtime: node.runtime || (deployment[node.id] ? 'docker' : 'process'),
    execution: node.execution || (deployment[node.id] ? 'container' : 'local'),
    lifecycle: node.lifecycle || 'managed', control: node.control || 'graphx',
    accelerator: node.runtime === 'qemu' ? selectedQemuAccelerator || node.accelerator || 'unknown' : null,
    requestedAccelerator: node.runtime === 'qemu' ? requestedQemuAccelerator || node.accelerator || 'unknown' : null,
    selectedAccelerator: node.runtime === 'qemu' ? selectedQemuAccelerator || 'unknown' : null,
    actualAccelerator: null, acceleratorEvidence: null,
    guestArchitecture: node.runtime === 'qemu' ? node.architecture || 'unknown' : null,
    input: node.ports.some(port => port.direction === 'input'),
    output: node.ports.some(port => port.direction === 'output'),
  }))
  const graphEdges = graph.edges.map(edge => {
    const [source] = edge.from.split('.')
    const [target, targetPort] = edge.to.split('.')
    const targetNode = graph.nodes.find(node => node.id === target)
    const schema = targetNode?.ports.find(port => port.name === targetPort)?.schema || 'unknown'
    const settings = transport[edge.transport]?.[edge.id] || {}
    return { id: edge.id, source, target, transport: edge.transport,
      dataPlane: edge.data_plane || 'graphx', framing: settings.framing || 'u32be',
      observationSource: edge.data_plane === 'external' ? externalObservationSource : 'runtime',
      port: settings.port || null, schema }
  })
  const network = config.network || {}
  const infrastructure = new Map(graphNodes.map(node => [node.id, node]))
  for (const item of network.networks || []) infrastructure.set(item.id, {
    id: item.id, label: item.id, role: item.profile,
    image: (item.subnets || [item.subnet]).join(', '), input: true, output: true,
  })
  for (const item of network.switches || []) infrastructure.set(item.id, {
    id: item.id, label: item.id, role: 'Open vSwitch',
    image: item.mirror ? `SPAN · ${item.mirror.id}` : item.datapath || 'system',
    input: true, output: true,
  })
  for (const item of network.routers || []) infrastructure.set(item.id, {
    id: item.id, label: item.id, role: item.kind.replaceAll('_', ' '),
    image: item.forwarding === false ? 'forwarding off' : 'IPv4 forwarding',
    input: true, output: true,
  })
  const qemu = graphNodes.find(node => node.runtime === 'qemu')
  const paths = structuredClone(network.edge_paths || {})
  if (qemu) {
    const boundary = { id: `${qemu.id}-host-runtime`, label: 'Host QEMU process', role: 'Host runtime',
      image: 'externally managed', hierarchy: 'host', input: true, output: true }
    infrastructure.set(boundary.id, boundary)
    infrastructure.set(`${qemu.id}-guest-app`, { id: `${qemu.id}-guest-app`,
      label: 'Raw TCP/UDP guest application', role: 'Guest application', image: qemu.guestArchitecture,
      hierarchy: 'guest', parent: qemu.id, input: true, output: true })
    infrastructure.set(qemu.id, { ...infrastructure.get(qemu.id), hierarchy: 'virtual-machine',
      parent: boundary.id })
    for (const edge of graphEdges) {
      const path = paths[edge.id]
      if (!Array.isArray(path)) continue
      const replacement = edge.source === qemu.id
        ? [`${qemu.id}-guest-app`, qemu.id, boundary.id]
        : [boundary.id, qemu.id, `${qemu.id}-guest-app`]
      paths[edge.id] = path.flatMap(id => id === qemu.id ? replacement : [id])
    }
  }
  return { graph: graph.id, nodes: graphNodes, edges: graphEdges,
    networkNodes: [...infrastructure.values()], edgePaths: paths }
}

export function topologyView(topology, evidence, diagnosticEvidence = null, diagnosticLayers = new Map()) {
  const withDiagnostics = diagnosticEvidence ? { ...topology,
    edges: topology.edges.map(edge => ({ ...edge,
      ...(diagnosticEvidence.flows[edge.id] ? {
        diagnosticState: diagnosticEvidence.flows[edge.id].state,
        diagnosticEvidence: diagnosticEvidence.flows[edge.id].evidence,
        diagnosticLayer: diagnosticLayers.get(diagnosticEvidence.flows[edge.id].state),
      } : {}),
    })),
    networkDiagnostic: { routeApplied: diagnosticEvidence.routeApplied,
      updatedAt: diagnosticEvidence.updatedAt },
  } : topology
  if (!evidence) return withDiagnostics
  const qemuNode = topology.nodes.find(node => node.runtime === 'qemu')
  const runtimeFields = { requestedAccelerator: evidence.requestedAccelerator || 'unknown',
    selectedAccelerator: evidence.selectedAccelerator || 'unknown',
    actualAccelerator: evidence.actualAccelerator || null,
    accelerator: evidence.actualAccelerator || evidence.selectedAccelerator || 'unknown',
    acceleratorEvidence: evidence.evidenceSource || null,
    vmState: evidence.vmState || null, guestState: evidence.guestState || null,
    guestProtocols: evidence.guestProtocols || null }
  const boundaryState = evidence.state === 'stopped' ? 'stopped'
    : evidence.vmState === 'unavailable' ? 'unavailable' : 'running'
  return { ...withDiagnostics,
    nodes: topology.nodes.map(node => node.runtime === 'qemu' ? { ...node, ...runtimeFields } : node),
    networkNodes: topology.networkNodes.map(node => {
      if (!qemuNode) return node
      if (node.id === qemuNode.id) return { ...node, ...runtimeFields,
        status: evidence.vmState || evidence.state, runtimeLayer: 'vm' }
      if (node.id === `${qemuNode.id}-guest-app`) return { ...node,
        status: evidence.guestState || evidence.state, runtimeLayer: 'guest',
        guestState: evidence.guestState || null, guestProtocols: evidence.guestProtocols || null }
      if (node.id === `${qemuNode.id}-host-runtime`)
        return { ...node, status: boundaryState, runtimeLayer: 'boundary' }
      return node
    }) }
}
