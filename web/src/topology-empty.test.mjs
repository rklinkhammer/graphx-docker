import test from 'node:test'
import assert from 'node:assert/strict'
import { applicationNodes, applicationEdges, infrastructureNodes, networkEdges } from './data/topology.js'

test('missing telemetry and zero-application graphs never fabricate a sample topology', () => {
  for (const topology of [undefined, {}, { nodes: [], edges: [], networkNodes: [], edgePaths: {} }]) {
    assert.deepEqual(applicationNodes(topology), [])
    assert.deepEqual(applicationEdges(topology), [])
    assert.deepEqual(infrastructureNodes(topology), [])
    assert.deepEqual(networkEdges('', topology), [])
  }
})
