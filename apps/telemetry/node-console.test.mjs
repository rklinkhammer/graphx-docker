import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync, chmodSync, rmSync, symlinkSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { EventEmitter } from 'node:events'
import { NodeConsole, nodeConsoleRequest } from './node-console.mjs'

function fixture(t) {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-console-'))
  let now = 100000, permitted = true
  const snapshot = { version: 1, graph: 'test', node: 'guest', generation: `console-${'a'.repeat(32)}`,
    runtime: '42:start', serial_identity: '1:2', status: 'running', updated: now / 1000, hex: '626f6f740a' }
  const publish = () => { rmSync(join(directory, 'guest.json'), { force: true }); writeFileSync(join(directory, 'guest.json'), JSON.stringify(snapshot), { mode: 0o444 }) }
  publish()
  const sockets = [], audit = []
  const console_ = new NodeConsole({ directory, graph: 'test', nodes: [{ node_id: 'guest', execution: { kind: 'qemu' } },
    { node_id: 'external', execution: { kind: 'external' } }], now: () => now,
    endpointStat: () => ({ isSocket: () => true, uid: 65532, dev: 1, ino: 2 }),
    connect: () => {
      const socket = new EventEmitter(); socket.writableLength = 0; socket.writes = []
      socket.write = data => socket.writes.push(data); socket.destroy = () => { socket.destroyed = true }
      sockets.push(socket); return socket
    }, audit: event => audit.push(event), validWriter: () => permitted })
  t.after(() => { console_.close(); rmSync(directory, { recursive: true, force: true }) })
  return { console_, directory, snapshot, publish, sockets, audit, advance: ms => { now += ms }, revoke: () => { permitted = false } }
}

test('logs validate graph identity, paths, bounds and immutable regular snapshots', t => {
  const f = fixture(t)
  assert.equal(f.console_.read('guest').hex, '626f6f740a')
  assert.throws(() => f.console_.read('../guest'), /unknown node/)
  f.snapshot.graph = 'other'; f.publish(); assert.throws(() => f.console_.read('guest'), /invalid console snapshot/)
  f.snapshot.graph = 'test'; f.publish(); chmodSync(join(f.directory, 'guest.json'), 0o644)
  assert.throws(() => f.console_.read('guest'), /invalid console snapshot/)
  f.publish(); rmSync(join(f.directory, 'guest.json')); symlinkSync('/etc/passwd', join(f.directory, 'guest.json'))
  assert.throws(() => f.console_.read('guest'))
})

test('serial has bounded byte replay, exclusive writer, generation checks, input bounds and revocation', t => {
  const f = fixture(t), c = f.console_
  assert.throws(() => c.view('external'), /QEMU/)
  const view = c.view('guest'); assert.equal(view.status, 'connecting')
  const socket = f.sockets[0]; socket.emit('connect')
  socket.emit('data', Buffer.alloc(70000, 65))
  assert.equal(c.view('guest').start, 70000 - 65536)
  assert.equal(c.view('guest').hex.length, 131072)
  const principal = { id: 'operator' }
  const lease = c.command('guest', { action: 'acquire', generation: view.generation }, principal, 'secret')
  assert.throws(() => c.command('guest', { action: 'acquire', generation: view.generation }, principal, 'secret'), /already held/)
  const input = { action: 'input', generation: view.generation, lease: lease.lease, hex: '030a' }
  c.command('guest', input, principal, 'secret'); assert.deepEqual(socket.writes[0], Buffer.from([3, 10]))
  assert.throws(() => c.command('guest', { ...input, hex: 'ff'.repeat(4097) }, principal, 'secret'), /invalid serial input/)
  assert.throws(() => c.command('guest', { ...input, lease: 'wrong' }, principal, 'secret'), /lease required/)
  assert.throws(() => c.command('guest', { ...input, generation: 'old' }, principal, 'secret'), /generation changed/)
  socket.writableLength = 9000; assert.throws(() => c.command('guest', input, principal, 'secret'), /backpressure/)
  f.revoke(); c.sweep(); assert.equal(c.view('guest').writer, null)
  assert.ok(!JSON.stringify(f.audit).includes('secret'))
  f.snapshot.runtime = '43:new'; f.publish(); c.sweep(); assert.equal(socket.destroyed, true)
})

test('writer expires, stale logs reject serial, idle viewers close without guest operations', t => {
  const f = fixture(t), c = f.console_; const view = c.view('guest'); f.sockets[0].emit('connect')
  c.command('guest', { action: 'acquire', generation: view.generation }, { id: 'operator' }, 'secret')
  f.advance(11000); c.sweep(); assert.equal(c.view('guest').writer, null)
  f.advance(20000); assert.equal(c.read('guest').stale, true)
  assert.throws(() => c.view('guest'), /not available/); c.sweep(); assert.equal(f.sockets[0].destroyed, true)
})

test('HTTP boundary denies observation, serial scope, origin and unknown node access', async t => {
  const f = fixture(t); let result
  const context = { json: (_r, status, body) => { result = { status, body } }, observationToken: 'required',
    authorized: () => false, controlPrincipal: () => ({ id: 'op' }), serialPermitted: () => false,
    requestOriginAllowed: () => true, controlPlane: { deny() {} } }
  const request = { method: 'GET', headers: {} }
  await nodeConsoleRequest(context, f.console_, request, {}, new URL('http://localhost/api/nodes/guest/logs'))
  assert.equal(result.status, 401)
  context.authorized = () => true
  await nodeConsoleRequest(context, f.console_, request, {}, new URL('http://localhost/api/nodes/wrong/logs'))
  assert.equal(result.status, 404)
  request.method = 'POST'
  await nodeConsoleRequest(context, f.console_, request, {}, new URL('http://localhost/api/control/serial/guest'))
  assert.equal(result.status, 403)
  context.requestOriginAllowed = () => false
  await nodeConsoleRequest(context, f.console_, request, {}, new URL('http://localhost/api/control/serial/guest'))
  assert.equal(result.status, 403)
})

test('browser-session deadline bounds writer even while its underlying credential remains valid', t => {
  const f = fixture(t), c = f.console_; const view = c.view('guest'); f.sockets[0].emit('connect')
  c.command('guest', { action: 'acquire', generation: view.generation }, { id: 'operator' }, 'secret', 101000)
  f.advance(2000); c.sweep(); assert.equal(c.view('guest').writer, null)
})

test('HTTP rejects malformed, oversized and unknown serial bodies without writing to guest', async t => {
  const f = fixture(t); let result
  const context = { json: (_r, status, body) => { result = { status, body } },
    controlPrincipal: () => ({ id: 'op' }), serialPermitted: () => true,
    requestOriginAllowed: () => true, withinRateLimit: () => true, controlPlane: { deny() {} } }
  for (const [body, expected] of [['{', 400], ['x'.repeat(10001), 413], ['{"action":"exec","command":"whoami"}', 400]]) {
    const request = { method: 'POST', headers: { 'content-type': 'application/json' }, destroy() {},
      async *[Symbol.asyncIterator]() { yield Buffer.from(body) } }
    await nodeConsoleRequest(context, f.console_, request, {}, new URL('http://localhost/api/control/serial/guest'))
    assert.equal(result.status, expected)
    assert.equal(f.sockets.length, 0)
  }
})


test('same-name serial socket replacement invalidates an existing session', t => {
  const f = fixture(t), c = f.console_; c.view('guest'); f.sockets[0].emit('connect')
  c.endpointStat = () => ({ isSocket: () => true, uid: 65532, dev: 1, ino: 3 })
  c.sweep(); assert.equal(f.sockets[0].destroyed, true)
  assert.throws(() => c.view('guest'), /endpoint unavailable/)
})
