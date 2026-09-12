import assert from 'node:assert/strict'
import test from 'node:test'
import { createTopology, topologyView } from './topology.mjs'

const config = {
  graph: { id: 'test', nodes: [
    { id: 'source', kind: 'source', runtime: 'process', execution: 'local',
      lifecycle: 'managed', control: 'graphx', accelerator: null, architecture: null,
      ports: [{ name: 'out', direction: 'output', schema: 'bytes' }] },
    { id: 'guest', kind: 'transform', runtime: 'qemu', execution: 'host', architecture: 'ppc',
      lifecycle: 'external', control: 'origin', accelerator: 'auto',
      ports: [{ name: 'in', direction: 'input', schema: 'bytes' }] },
  ], edges: [{ id: 'flow', from: { node: 'source', port: 'out' },
    to: { node: 'guest', port: 'in' }, data_plane: 'graphx',
    transport: { kind: 'tcp', port: 9001, framing: 'u32be' } }] },
  deployment: { project: null, services: [], telemetry: { service: null, port: 0 } },
  network: { backend: 'ovs', networks: [], switches: [{ id: 'switch', datapath: 'system' }],
    routers: [], attachments: [], captures: [], faults: [],
    edge_paths: [{ edge_id: 'flow', hops: ['source', 'switch', 'guest'] }] },
  observability: { capture: { provider: 'packet-stream' } },
}

test('topology construction preserves runtime hierarchy and edge semantics', () => {
  const topology = createTopology(config, { GRAPHX_QEMU_ACCEL: 'tcg', GRAPHX_QEMU_REQUESTED_ACCEL: 'auto' })
  assert.deepEqual(topology.edgePaths.flow,
    ['source', 'switch', 'guest-host-runtime', 'guest', 'guest-guest-app'])
  assert.deepEqual(topology.edges[0], { id: 'flow', source: 'source', target: 'guest', transport: 'tcp',
    dataPlane: 'graphx', framing: 'u32be', observationSource: 'runtime', port: 9001, schema: 'bytes' })
  assert.equal(topology.nodes[1].selectedAccelerator, 'tcg')
})

test('topology view overlays bounded runtime and diagnostic evidence', () => {
  const topology = createTopology(config)
  const view = topologyView(topology, { state: 'ready', updatedAt: 1, vmState: 'running',
    guestState: 'ready', selectedAccelerator: 'tcg' },
  { routeApplied: false, updatedAt: 2,
    flows: { flow: { state: 'allowed', evidence: 'receiver-confirmed' } } },
  new Map([['allowed', 'application']]))
  assert.equal(view.nodes[1].accelerator, 'tcg')
  assert.equal(view.edges[0].diagnosticLayer, 'application')
  assert.equal(view.networkDiagnostic.updatedAt, 2)
})
