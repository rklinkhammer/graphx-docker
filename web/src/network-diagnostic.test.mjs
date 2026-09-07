import assert from 'node:assert/strict'
import test from 'node:test'
import { applicationEdges, networkEdges } from './data/topology.js'

const topology = {
  edges: [{ id: 'routed-flow', source: 'left', target: 'right', transport: 'udp',
    diagnosticState: 'missing-route', diagnosticEvidence: 'route-absent' }],
  edgePaths: { 'routed-flow': ['left', 'left-net', 'router', 'right-net', 'right'] },
}

test('network diagnostics remain visible on application and routed hop edges', () => {
  const [application] = applicationEdges(topology)
  assert.equal(application.data.diagnosticState, 'missing-route')
  assert.equal(application.data.diagnosticEvidence, 'route-absent')
  const hops = networkEdges('routed-flow', topology)
  assert.equal(hops.length, 4)
  assert.ok(hops.every(edge => edge.data.diagnosticState === 'missing-route'))
  assert.ok(hops.every(edge => edge.data.diagnosticEvidence === 'route-absent'))
  assert.ok(hops.every(edge => edge.data.highlighted))
})
