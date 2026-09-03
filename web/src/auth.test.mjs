import assert from 'node:assert/strict'
import test from 'node:test'
import { bearerHeaders, controlCommandRequest, controlRequest, controlStatusRequest, persistObservationToken, webSocketProtocols } from './auth.js'

test('observation credentials use bearer headers and UTF-8 WebSocket subprotocols', () => {
  const token = 'graphx-observation-token-安全-0123456789'
  assert.deepEqual(bearerHeaders(token), { Authorization: `Bearer ${token}` })
  const protocols = webSocketProtocols(token)
  assert.equal(protocols[0], 'graphx')
  const decoded = Buffer.from(protocols[1].slice('graphx-auth.'.length), 'base64url').toString('utf8')
  assert.equal(decoded, token)
  assert.deepEqual(webSocketProtocols(), ['graphx'])
})

test('command requests carry bounded intent and an idempotency identity', () => {
  const request = controlCommandRequest('pause', 'control-token', ['generator'], 'maintenance', 'request-1')
  assert.equal(request.url, '/api/control/commands')
  assert.deepEqual(request.options.headers, { Authorization: 'Bearer control-token',
    'Content-Type': 'application/json', 'Idempotency-Key': 'request-1' })
  assert.deepEqual(JSON.parse(request.options.body), { action: 'pause', targetNodes: ['generator'],
    reason: 'maintenance' })
})

test('observation persistence is session-scoped through the supplied storage', () => {
  const values = new Map()
  const storage = {
    setItem: (key, value) => values.set(key, value),
    removeItem: key => values.delete(key),
  }
  persistObservationToken(storage, 'observation-token-012345678901234')
  assert.equal(values.get('graphx-observation-token'), 'observation-token-012345678901234')
  persistObservationToken(storage, '')
  assert.equal(values.has('graphx-observation-token'), false)
})

test('every control action, including reset, carries only the in-memory bearer', () => {
  const token = 'control-token-0123456789012345678'
  for (const action of ['pause', 'resume', 'reset'])
    assert.deepEqual(controlRequest(action, token), {
      url: `/api/control/${action}`,
      options: { method: 'POST', headers: { Authorization: `Bearer ${token}` } },
    })
})

test('command status requests retain the in-memory control bearer', () => {
  assert.deepEqual(controlStatusRequest('command/id', 'control-token'), {
    url: '/api/control/commands/command%2Fid',
    options: { method: 'GET', headers: { Authorization: 'Bearer control-token' } },
  })
})
