export const initialNodes = [
  { id: 'generator', position: { x: 40, y: 120 }, data: { label: 'Generator', role: 'Source', status: 'healthy', cpu: null, image: 'graphx-demo:latest', input: false, output: true } },
  { id: 'transform', position: { x: 350, y: 120 }, data: { label: 'Transform', role: 'Processor', status: 'healthy', cpu: null, image: 'graphx-demo:latest', input: true, output: true } },
  { id: 'sink', position: { x: 660, y: 120 }, data: { label: 'Sink', role: 'Consumer', status: 'healthy', cpu: null, image: 'graphx-demo:latest', input: true, output: false } },
]

export const initialEdges = [
  { id: 'samples', source: 'generator', target: 'transform', type: 'telemetry', data: { label: 'samples', rate: '—', messages: '—', latency: '—', drops: '—', connection: 'unavailable', reconnects: '—', backpressure: '—', port: 7001, schema: 'Sample' } },
  { id: 'transformed', source: 'transform', target: 'sink', type: 'telemetry', data: { label: 'transformed', rate: '—', messages: '—', latency: '—', drops: '—', connection: 'unavailable', reconnects: '—', backpressure: '—', port: 7002, schema: 'TransformedSample' } },
]

export const networkNodes = [
  { id: 'generator', data: { label: 'Generator', role: 'Application', status: 'preview', cpu: 3, image: '10.10.0.10', input: false, output: true } },
  { id: 'gx-mac-domain', data: { label: 'macvlan L2', role: 'Docker network', status: 'modeled', cpu: 0, image: '10.10.0.0/24', input: true, output: true } },
  { id: 'br-gx-mac', data: { label: 'br-gx-mac', role: 'Open vSwitch', status: 'mirrored', cpu: 0, image: 'SPAN · cap-mac', input: true, output: true } },
  { id: 'domain-router', data: { label: 'Domain router', role: 'Linux namespace', status: 'forwarding', cpu: 0, image: '.1 ↔ .1', input: true, output: true } },
  { id: 'br-gx-ipv', data: { label: 'br-gx-ipv', role: 'Open vSwitch', status: 'mirrored', cpu: 0, image: 'SPAN · cap-ipv', input: true, output: true } },
  { id: 'gx-ipv-domain', data: { label: 'ipvlan L2', role: 'Docker network', status: 'modeled', cpu: 0, image: '10.20.0.0/24', input: true, output: true } },
  { id: 'transform', data: { label: 'Transform', role: 'Application', status: 'preview', cpu: 17, image: '10.20.0.20', input: true, output: true } },
  { id: 'sink', data: { label: 'Sink', role: 'Application', status: 'preview', cpu: 6, image: '10.20.0.30', input: true, output: false } },
]

export const edgePaths = {
  samples: ['generator', 'gx-mac-domain', 'br-gx-mac', 'domain-router', 'br-gx-ipv', 'gx-ipv-domain', 'transform'],
  transformed: ['transform', 'gx-ipv-domain', 'sink'],
}

export function applicationNodes(topology) {
  if (!topology?.nodes?.length) return initialNodes
  return topology.nodes.map(node => ({ id: node.id, data: {
    label: node.label, role: node.role, status: 'starting', cpu: null,
    image: node.image, input: node.input, output: node.output,
  }}))
}

export function applicationEdges(topology) {
  if (!topology?.edges?.length) return initialEdges
  return topology.edges.map(edge => ({ id: edge.id, source: edge.source, target: edge.target,
    type: 'telemetry', data: { label: edge.id, rate: '—', messages: '—', latency: '—',
      drops: '—', connection: 'unavailable', reconnects: '—', backpressure: '—',
      port: edge.port, schema: edge.schema, transport: edge.transport } }))
}

export function infrastructureNodes(topology) {
  if (!topology?.networkNodes?.length) return networkNodes
  return topology.networkNodes.map(node => ({ id: node.id, data: {
    label: node.label, role: node.role, status: 'modeled', cpu: null,
    image: node.image, input: node.input, output: node.output,
  }}))
}

export function networkEdges(selectedId, topology) {
  const edges = []
  const paths = topology?.edgePaths || edgePaths
  for (const [logicalEdge, path] of Object.entries(paths)) {
    path.slice(0, -1).forEach((source, index) => edges.push({
      id: `${logicalEdge}-hop-${index}`, source, target: path[index + 1], type: 'telemetry',
      data: { logicalEdge, highlighted: logicalEdge === selectedId,
        rate: '—', latency: index === 3 ? 'router' : 'L2' },
    }))
  }
  return edges
}
