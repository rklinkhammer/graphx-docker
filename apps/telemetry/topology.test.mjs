import assert from 'node:assert/strict'
import test from 'node:test'
import { createTopology, topologyView } from './topology.mjs'

const config = {
  graph: { id: 'test', nodes: [
    { id: 'source', kind: 'source', ports: [{ name: 'out', direction: 'output', schema: 'bytes' }] },
    { id: 'guest', kind: 'transform', runtime: 'qemu', execution: 'host', architecture: 'ppc',
      ports: [{ name: 'in', direction: 'input', schema: 'bytes' }] },
  ], edges: [{ id: 'flow', from: 'source.out', to: 'guest.in', transport: 'tcp' }] },
  transport: { tcp: { flow: { port: 9001, framing: 'u32be' } } },
  network: { switches: [{ id: 'switch', datapath: 'system' }],
    edge_paths: { flow: ['source', 'switch', 'guest'] } },
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
