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
