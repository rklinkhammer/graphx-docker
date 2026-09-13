import test from 'node:test'
import assert from 'node:assert/strict'
import { Writable } from 'node:stream'
import { boundOutput } from './output-bound.mjs'

test('managed output shares a byte budget and acknowledges discarded writes', async () => {
  const chunks = []
  const make = () => new Writable({ write(chunk, encoding, callback) { chunks.push(chunk); callback() } })
  const out = make(), error = make()
  boundOutput([out, error], '4096')
  out.write('a'.repeat(4000))
  error.write('b'.repeat(4000))
  await new Promise(resolve => out.write('discarded', resolve))
  assert.equal(Buffer.concat(chunks).length, 4096)
  assert.equal(Buffer.concat(chunks).subarray(4000).toString(), 'b'.repeat(96))
})
test('invalid managed output bounds fail closed', () => {
  for (const value of ['0', '4095', '-1', '1e6', '16777217', 'invalid'])
    assert.throws(() => boundOutput([], value), /E_LOG_BOUND/)
})
