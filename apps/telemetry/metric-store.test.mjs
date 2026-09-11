import assert from 'node:assert/strict'
import test from 'node:test'
import { createMetricStore, currentRates, edgeView } from './metric-store.mjs'

const topology = { nodes: [{ id: 'a' }, { id: 'b' }],
  edges: [{ id: 'flow', source: 'a', target: 'b' }] }

test('metric store ingests sends and receives without changing snapshot fields', () => {
  const store = createMetricStore(topology)
  store.ingest({ kind: 'trace', nodeId: 'a', edgeId: 'flow', event: 'send', sequence: 4,
    wireBytes: 64, cpuPercent: 12.5 }, 10_000)
  store.ingest({ kind: 'trace', nodeId: 'b', edgeId: 'flow', event: 'receive', sequence: 4,
    wireBytes: 64, latencyUs: 51 }, 10_001)
  const view = edgeView(store.edges.flow, 10_001)
  assert.equal(view.sent, 1)
  assert.equal(view.received, 1)
  assert.equal(view.p95LatencyUs, 100)
  assert.deepEqual(currentRates(store.edges.flow, 10_001), { messageRate: 1, byteRate: 64 })
  assert.equal(store.nodes.a.cpuPercent, 12.5)
})

test('metric store bounds recent capture correlation and retains connection on reset', () => {
  const store = createMetricStore(topology)
  store.ingest({ kind: 'network_packet', nodeId: 'a', edgeId: 'flow', event: 'frame',
    traceId: 'trace', sequence: 1, wireBytes: 8 }, 1)
  store.recordCapture({ edgeId: 'flow', traceId: 'trace', sequence: 1, file: 'a.pcapng' })
  assert.equal(store.recentWithCapture()[0].captures.length, 1)
  store.reset()
  assert.equal(store.edges.flow.sent, 0)
  assert.equal(store.edges.flow.connection, 'connected')
  assert.deepEqual(store.recentWithCapture(), [])
})
