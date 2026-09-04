import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
import Ajv2020 from 'ajv/dist/2020.js'
import { parse as parseYaml } from 'yaml'

const here = dirname(fileURLToPath(import.meta.url))
const repository = resolve(here, '../..')
const schema = JSON.parse(readFileSync(resolve(repository, 'config/schema/graphx.schema.json'), 'utf8'))
const base = parseYaml(readFileSync(resolve(repository, 'graphx.yaml'), 'utf8'))
// Existing schema composition uses inherited types and `required` properties
// through allOf. Ajv's optional strictTypes/strictRequired lints reject those
// valid Draft 2020-12 forms, so disable only the two non-semantic lints.
const validate = new Ajv2020({ allErrors: true, strictRequired: false,
  strictTypes: false }).compile(schema)

function withCapture(capture) {
  const configuration = structuredClone(base)
  if (capture == null) delete configuration.observability.capture
  else configuration.observability.capture = capture
  return configuration
}

function expectValid(capture, label) {
  assert.equal(validate(withCapture(capture)), true,
    `${label}: ${JSON.stringify(validate.errors)}`)
}

function expectInvalid(capture, label) {
  assert.equal(validate(withCapture(capture)), false, `${label}: unexpectedly valid`)
}

test('capture JSON schema matches native conditional and scalar semantics', () => {
  expectValid(null, 'omitted capture')
  expectValid({ enabled: false }, 'disabled capture')
  expectValid({ enabled: true, provider: 'ovs-span' }, 'external capture')
  expectValid({ enabled: true, provider: 'pcapng', directory: 'captures', snaplen: 256,
    max_file_bytes: 65536, max_packets: 1 }, 'minimum PCAPNG limits')
  expectValid({ enabled: true, provider: 'pcapng', directory: 'captures', snaplen: 16777220,
    max_file_bytes: 4294967296, max_packets: 100000000 }, 'maximum PCAPNG limits')

  expectInvalid({ enabled: true }, 'enabled capture without provider')
  expectInvalid({ enabled: true, provider: 'pcapng' }, 'PCAPNG capture without directory')
  expectValid({ enabled: false, provider: 'pcapng' }, 'disabled PCAPNG without directory')
  expectInvalid({ enabled: true, provider: 'pcapgn', directory: 'captures' }, 'unknown provider')
  expectInvalid({ enabled: 'true', provider: 'ovs-span' }, 'quoted boolean')
  expectInvalid({ enabled: true, provider: 'pcapng', directory: 'captures', snaplen: '4096' },
    'quoted integer')
})

function withUdp(settings) {
  const configuration = structuredClone(base)
  configuration.graph.edges[0].transport = 'udp'
  configuration.transport.udp = { [configuration.graph.edges[0].id]: settings }
  return configuration
}

test('UDP JSON schema enforces scalar types and numeric boundaries', () => {
  const minimum = { mode: 'unicast', destination: '127.0.0.1', bind: '0.0.0.0', port: 1,
    ttl: 0, receive_buffer_bytes: 4096, send_buffer_bytes: 4096,
    max_datagram_bytes: 64, framing: 'u32be' }
  assert.equal(validate(withUdp(minimum)), true, JSON.stringify(validate.errors))
  const maximum = { ...minimum, mode: 'multicast', destination: '239.255.42.1', port: 65535,
    ttl: 255, receive_buffer_bytes: 268435456, send_buffer_bytes: 268435456,
    max_datagram_bytes: 65507, loopback: true, reuse_address: true, interface: '127.0.0.1' }
  assert.equal(validate(withUdp(maximum)), true, JSON.stringify(validate.errors))
  for (const [field, value] of [['port', 0], ['ttl', 256], ['receive_buffer_bytes', 4095],
    ['send_buffer_bytes', 268435457], ['max_datagram_bytes', 65508], ['loopback', 'true'],
    ['framing', 'raw']]) {
    assert.equal(validate(withUdp({ ...minimum, [field]: value })), false,
      `${field}=${value} unexpectedly valid`)
  }
})
