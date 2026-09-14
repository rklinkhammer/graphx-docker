import test from 'node:test'
import assert from 'node:assert/strict'
import { decodeHex, logText, serialDelta } from './nodeConsole.mjs'

test('serial cursors preserve byte chunks and report lost replay or new generations', () => {
  const initial = { generation: 'one', start: 0, end: 2, hex: 'e282' }
  const next = { generation: 'one', start: 0, end: 3, hex: 'e282ac' }
  const decoder = new TextDecoder()
  assert.equal(decoder.decode(serialDelta(null, initial).bytes, { stream: true }), '')
  assert.equal(decoder.decode(serialDelta(initial, next).bytes, { stream: true }), '€')
  assert.equal(serialDelta(next, { generation: 'one', start: 5, end: 6, hex: '61' }).gap, true)
  assert.equal(serialDelta(next, { generation: 'two', start: 0, end: 1, hex: '61' }).reset, true)
  assert.throws(() => serialDelta(next, { ...next, end: 999 }), /cursor/)
})
test('logs are text and download bytes are bounded and validated', () => {
  assert.equal(logText({ hex: '3c7363726970743e' }), '<script>')
  assert.throws(() => decodeHex('zz'), /Invalid/)
  assert.throws(() => decodeHex('00'.repeat(65537)), /Invalid/)
})

test('repeated serial polls do not replay output, and malformed bytes fail closed', () => {
  const value = { generation: 'one', start: 0, end: 1, hex: '61' }
  assert.equal(serialDelta(value, value).bytes.length, 0)
  assert.throws(() => serialDelta(null, { ...value, hex: '<script>' }), /Invalid/)
})
