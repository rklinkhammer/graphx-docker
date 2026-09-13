import assert from 'node:assert/strict'
import test from 'node:test'
import { createTopology, topologyView } from './topology.mjs'

const config = {
  graph_id: 'test', nodes: [
    { node_id: 'source', type: 'source', execution: {kind: 'native'},
      bindings: {out: [{role: 'connect', schema: 'bytes'}]} },
    { node_id: 'guest', type: 'guest.echo', execution: {kind: 'qemu', architecture: 'x86_64', accelerator: 'tcg'},
      bindings: {in: [{role: 'listen', schema: 'bytes'}]} },
  ], connections: [{id: 'flow', from: {node: 'source', port: 'out'}, to: {node: 'guest', port: 'in'},
    encoding: 'graphx', transport: 'tcp', settings: {port: 9001, framing: 'u32be'}}],
  network: {networks: [], switches: [{id: 'switch', datapath: 'system'}], routers: [], attachments: [], captures: [],
    edge_paths: {flow: ['source', 'switch', 'guest']}},
  platform: {capture: {provider: 'application'}},
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

test('multiple guest instances do not share unscoped runtime evidence', () => {
  const multiple = structuredClone(config)
  multiple.nodes.push({...structuredClone(config.nodes[1]), node_id: 'guest-two'})
  const topology = createTopology(multiple)
  assert.ok(topology.networkNodes.some(n => n.id === 'guest-two-host-runtime'))
  const unscoped = topologyView(topology, {state: 'ready', actualAccelerator: 'tcg'})
  assert.ok(unscoped.nodes.filter(n => n.runtime === 'qemu').every(n => n.actualAccelerator === null))
  const scoped = topologyView(topology, {nodeId: 'guest-two', state: 'ready', actualAccelerator: 'tcg'})
  assert.equal(scoped.nodes.find(n => n.id === 'guest').actualAccelerator, null)
  assert.equal(scoped.nodes.find(n => n.id === 'guest-two').actualAccelerator, 'tcg')
})
