import assert from 'node:assert/strict'
import test from 'node:test'
import { createConsoleSessions } from './console-session.mjs'

function fixture(extra = {}) {
  let time = 0, revoked = false
  const sessions = createConsoleSessions({ graph: 'test', observe: value => value === 'observe' && !revoked,
    control: value => value === 'control' && !revoked, originAllowed: () => true, json: () => {},
    now: () => time, ...extra })
  return { sessions, advance: value => { time += value }, revoke: () => { revoked = true } }
}
const origin = 'http://127.0.0.1:8080'
const request = (cookie, extra = {}) => ({ url: '/api/topology', method: 'GET', headers: { cookie, ...extra } })

test('handoffs are single use, origin bound, bounded and expire', () => {
  const { sessions, advance } = fixture({ capacity: 1 })
  assert.throws(() => sessions.mint('wrong', null, origin))
  assert.throws(() => sessions.mint('observe', 'wrong', origin))
  const code = sessions.mint('observe', 'control', origin)
  assert.throws(() => sessions.mint('observe', null, origin))
  assert.throws(() => sessions.exchange(code, 'http://evil.test'))
  const result = sessions.exchange(code, origin)
  assert.throws(() => sessions.exchange(code, origin))
  assert.ok(sessions.get(request(result.cookie)))
  const expired = sessions.mint('observe', null, origin)
  advance(60_000)
  assert.throws(() => sessions.exchange(expired, origin))
  advance(8 * 60 * 60_000)
  assert.equal(sessions.get(request(result.cookie)), null)
})

test('cookies preserve explicit bearer precedence and require CSRF for controls', () => {
  const { sessions, revoke } = fixture({ secure: true })
  const { cookie, session } = sessions.exchange(sessions.mint('observe', 'control', origin), origin)
  assert.match(cookie, /; Secure$/)
  const observed = request(cookie)
  assert.equal(sessions.attach(observed), true)
  assert.equal(observed.headers.authorization, 'Bearer observe')
  assert.equal(sessions.attach(request(cookie, { authorization: 'Bearer explicit' })), false)
  assert.equal(sessions.attach({ ...request(cookie), url: '/api/control/reset', method: 'POST' }), false)
  const controlled = { ...request(cookie, { origin, 'x-graphx-csrf': session.csrf }), url: '/api/control/reset', method: 'POST' }
  assert.equal(sessions.attach(controlled), true)
  assert.equal(controlled.headers.authorization, 'Bearer control')
  assert.equal(sessions.get(request(cookie, { origin: 'http://evil.test' })), null)
  assert.equal(sessions.attach(request(cookie), true), false)
  revoke()
  assert.equal(sessions.get(request(cookie)), null)
})

test('observation sessions cannot become control sessions or cross graph instances', () => {
  const { sessions } = fixture()
  const { cookie, session } = sessions.exchange(sessions.mint('observe', null, origin), origin)
  assert.equal(sessions.attach({ ...request(cookie, { origin, 'x-graphx-csrf': session.csrf }),
    url: '/api/control/reset', method: 'POST' }), false)
  assert.equal(fixture().sessions.get(request(cookie)), null)
  assert.equal(fixture({ graph: 'other' }).sessions.get(request(cookie)), null)
})
