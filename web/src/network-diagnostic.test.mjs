import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import test from 'node:test'
import { applicationEdges, edgeObservationAvailable, trafficObservation, networkEdges } from './data/topology.js'

const topology = {
  edges: [{ id: 'routed-flow', source: 'left', target: 'right', transport: 'udp',
    diagnosticState: 'missing-route', diagnosticEvidence: 'route-absent',
    diagnosticLayer: 'route' }],
  edgePaths: { 'routed-flow': ['left', 'left-net', 'router', 'right-net', 'right'] },
}

test('unobserved raw edges do not manufacture zero counters or disconnected state', () => {
  const [edge] = applicationEdges({ edges: [{ id: 'vita', source: 'radio', target: 'processor',
    dataPlane: 'external', framing: 'none', observationSource: 'ovs-span' }] })
  assert.equal(edgeObservationAvailable(edge.data, { lastSeen: null, connection: 'disconnected' }), false)
  assert.equal(edgeObservationAvailable(edge.data, {}), false)
  assert.equal(edge.data.connection, 'unavailable')
  assert.equal(edge.data.rate, '—')
  assert.equal(edge.data.messages, '—')
  assert.equal(edgeObservationAvailable(edge.data, { lastSeen: 123 }), true)
  assert.equal(edgeObservationAvailable({ dataPlane: 'graphx' }, { lastSeen: null }), true)
})

test('network diagnostics remain visible on application and routed hop edges', () => {
  const [application] = applicationEdges(topology)
  assert.equal(application.data.diagnosticState, 'missing-route')
  assert.equal(application.data.diagnosticEvidence, 'route-absent')
  assert.equal(application.data.diagnosticLayer, 'route')
  const hops = networkEdges('routed-flow', topology)
  assert.equal(hops.length, 4)
  assert.ok(hops.every(edge => edge.data.diagnosticState === 'missing-route'))
  assert.ok(hops.every(edge => edge.data.diagnosticEvidence === 'route-absent'))
  assert.ok(hops.every(edge => edge.data.diagnosticLayer === 'route'))
  assert.ok(hops.every(edge => edge.data.highlighted))
})

test('network diagnostic failure layers have distinct visual semantics', async () => {
  const styles = await readFile(new URL('./styles.css', import.meta.url), 'utf8')
  assert.match(styles, /diagnostic-link-down/)
  assert.match(styles, /diagnostic-attachment-missing/)
  assert.match(styles, /diagnostic-application-unavailable/)
})

test('allowed and route-applied diagnostics use distinct visual semantics', async () => {
  const styles = await readFile(new URL('./styles.css', import.meta.url), 'utf8')
  assert.match(styles, /edge-path\.diagnostic-allowed \{ stroke: #48d49a; \}/)
  assert.match(styles, /edge-path\.diagnostic-route-applied \{ stroke: #69a8ff; stroke-dasharray:/)
  assert.match(styles, /edge-badge\.diagnostic-route-applied \{ border-color: #3f72b5;/)
  assert.doesNotMatch(styles, /diagnostic-allowed[^\n]*diagnostic-route-applied/)
})


test('traffic summary distinguishes measured data edges from unobserved control edges', () => {
  const data = Array.from({ length: 5 }, () => ({ received: 1000, messageRate: 980 }))
  const control = Array.from({ length: 4 }, () => ({ received: 0, messageRate: 0 }))
  assert.deepEqual(trafficObservation([...data, ...control], true, true),
    { observed: 5, total: 9, samples: 1000, flowing: false })
  assert.equal(trafficObservation(data, true, true).flowing, true)
  assert.equal(trafficObservation(data, false, true).flowing, false)
  assert.equal(trafficObservation(data, true, false).flowing, false)
  assert.equal(trafficObservation([{ received: 1000, messageRate: 0 }], true, true).observed, 0)
})
