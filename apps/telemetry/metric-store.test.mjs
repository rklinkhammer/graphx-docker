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

const total = (packets, timestamp, overrides = {}) => ({ kind: 'edge_totals', event: 'totals',
  nodeId: 'a', edgeId: 'flow', direction: 'sent', sessionId: 'a'.repeat(32),
  packets, wireBytes: packets * 100, timestamp, ...overrides })

test('raw totals recover lost reports without double counting endpoints or inventing latency', () => {
  const store = createMetricStore(topology)
  store.ingest(total(100, 1000), 1000)
  store.ingest(total(400, 4000), 4000) // two reports were lost
  store.ingest(total(395, 4000, { nodeId: 'b', direction: 'received' }), 4001)
  assert.equal(store.edges.flow.sent, 400)
  assert.equal(store.edges.flow.received, 395)
  assert.deepEqual(currentRates(store.edges.flow, 4001), { messageRate: 100, byteRate: 10000 })
  assert.equal(edgeView(store.edges.flow).meanLatencyUs, null)
  assert.deepEqual(currentRates(store.edges.flow, 10000), { messageRate: 0, byteRate: 0 })
  for (const event of [total(400, 4000), total(200, 2000), total(399, 5000),
    total(900, 5000, { nodeId: 'b' }), total(900, 5000, { direction: 'received' })])
    store.ingest(event, 5000)
  assert.equal(store.edges.flow.sent, 400)
  assert.equal(store.edges.flow.received, 395)
})

test('display resets preserve baselines and process restarts start a new counter session', () => {
  const store = createMetricStore(topology)
  store.ingest(total(100, 1000), 1000)
  store.reset()
  store.ingest(total(150, 2000), 2000)
  assert.equal(store.edges.flow.sent, 50)
  store.ingest(total(10, 3000, { sessionId: 'b'.repeat(32) }), 3000)
  assert.equal(store.edges.flow.sent, 60)
  store.ingest(total(170, 2500), 3100) // delayed old process
  assert.equal(store.edges.flow.sent, 60)
  store.ingest(total(20, 4000, { sessionId: 'b'.repeat(32) }), 4000)
  assert.deepEqual(currentRates(store.edges.flow, 4000), { messageRate: 10, byteRate: 1000 })
})
