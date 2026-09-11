import assert from 'node:assert/strict'
import test from 'node:test'
import { createHttpRequestHandler } from './http-routes.mjs'

function responseRecorder() {
  return { headersSent: false, writeHead(status, headers) { this.status = status; this.headers = headers; this.headersSent = true },
    end(body) { this.body = body }, destroy() { this.destroyed = true } }
}

test('HTTP routing retains the liveness contract and bounded request target', () => {
  const securityHeaders = {}
  const json = (response, status, value, extra = {}) => {
    response.writeHead(status, { ...securityHeaders, ...extra }); response.end(JSON.stringify(value))
  }
  const handler = createHttpRequestHandler({ json, securityHeaders })
  const live = responseRecorder()
  handler({ url: '/api/live', method: 'GET' }, live)
  assert.equal(live.status, 200)
  assert.deepEqual(JSON.parse(live.body), { status: 'live', service: 'graphx-telemetry' })
  const oversized = responseRecorder()
  handler({ url: `/${'x'.repeat(2048)}`, method: 'GET' }, oversized)
  assert.equal(oversized.status, 414)
})
