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

test('raw external edge and runtime metadata pass structural schema checks', () => {
  const configuration = structuredClone(base)
  configuration.graph.nodes[0] = { ...configuration.graph.nodes[0], runtime: 'docker',
    execution: 'container', lifecycle: 'managed', control: 'origin' }
  configuration.graph.nodes[1] = { ...configuration.graph.nodes[1], runtime: 'qemu',
    execution: 'host', lifecycle: 'external', control: 'none', accelerator: 'auto',
    architecture: 'x86_64' }
  configuration.graph.edges[0] = { ...configuration.graph.edges[0], data_plane: 'external' }
  configuration.transport.tcp[configuration.graph.edges[0].id].framing = 'none'
  assert.equal(validate(configuration), true, JSON.stringify(validate.errors))
  const physical = structuredClone(configuration)
  physical.graph.nodes[1].runtime = 'external'
  delete physical.graph.nodes[1].accelerator
  delete physical.graph.nodes[1].architecture
  assert.equal(validate(physical), true, JSON.stringify(validate.errors))

  for (const [field, value] of [['runtime', 'podman'], ['execution', 'remote'],
    ['lifecycle', 'automatic'], ['control', 'guest']]) {
    const invalid = structuredClone(configuration)
    invalid.graph.nodes[0][field] = value
    assert.equal(validate(invalid), false, `${field}=${value} unexpectedly valid`)
  }
  for (const [field, value] of [['accelerator', 'metal'], ['architecture', 'arm64']]) {
    const invalid = structuredClone(configuration)
    invalid.graph.nodes[1][field] = value
    assert.equal(validate(invalid), false, `${field}=${value} unexpectedly valid`)
  }
  const invalidPlane = structuredClone(configuration)
  invalidPlane.graph.edges[0].data_plane = 'raw'
  assert.equal(validate(invalidPlane), false, 'unknown data plane unexpectedly valid')
})

test('instance and SDR schema requires bounded settings and credential references', () => {
  const source = parseYaml(readFileSync(resolve(repository,
    'examples/sdr-node/two-source/graphx.yaml'), 'utf8'))
  assert.equal(validate(source), true, JSON.stringify(validate.errors))
  const mutations = [
    c => { delete c.deployment.instance_id },
    c => { c.deployment.instance_id = '' },
    c => { c.deployment.instance_id = 'a'.repeat(65) },
    c => { c.deployment.instance_id = 'lab\n' },
    c => { c.deployment.instance_id = true },
    c => { c.graph.nodes[0].sdr.frequency_hz = '100000000' },
    c => { c.graph.nodes[0].sdr.sample_interval_ms = 60001 },
    c => { c.graph.nodes[0].sdr.credentials.private_key = 'inline-secret' },
    c => { delete c.graph.nodes[0].sdr.credentials.ca_file },
    c => { c.graph.nodes[0].sdr.credentials.private_key_file = 'relative.key' },
    c => { c.graph.nodes[0].sdr.credentials.server_name = 'sdr-east\n' },
  ]
  for (const mutate of mutations) {
    const candidate = structuredClone(source)
    mutate(candidate)
    assert.equal(validate(candidate), false, JSON.stringify(candidate))
  }
})
