import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'
import Ajv2020 from 'ajv/dist/2020.js'
import { parse } from 'yaml'
const schema = JSON.parse(readFileSync(new URL('../../config/schema/graphx.schema.json', import.meta.url)))
const base = parse(readFileSync(new URL('../../examples/sample-pipeline/graphx.yml', import.meta.url), 'utf8'))
const validate = new Ajv2020({allErrors: true, strictRequired: false, strictTypes: false}).compile(schema)
test('authored v3 schema is closed and strictly typed', () => {
  assert.equal(validate(base), true, JSON.stringify(validate.errors))
  for (const capture of [{enabled: 'true'}, {max_files: 0}, {provider: 'pcapng'}, {directory: '/tmp'}, {max_packets: 10000001}]) {
    assert.equal(validate({...base, platform: {capture}}), false)
  }
  for (const version of [2, '3']) assert.equal(validate({...base, version}), false)
  assert.equal(validate({...base, graph: {...base.graph, nodes: []}}), false)
  assert.equal(validate({...base, platform: {capture: {enabled: true, provider: 'application', max_files: 64}}}), true)
})
test('transport settings enforce scalar and resource bounds', () => {
  for (const settings of [{port: 0}, {ttl: 256}, {max_datagram_bytes: 65508}, {receive_buffer_bytes: 16777217}, {loopback: 'true'}, {framing: 'raw'}]) {
    const value = structuredClone(base)
    value.connections.samples.settings = settings
    assert.equal(validate(value), false, JSON.stringify(settings))
  }
})
