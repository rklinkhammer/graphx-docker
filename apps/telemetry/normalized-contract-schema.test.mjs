import assert from 'node:assert/strict'
import { readdirSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
import { selectNormalizedNode } from './normalized-config.mjs'
import Ajv2020 from 'ajv/dist/2020.js'

const here = dirname(fileURLToPath(import.meta.url))
const repository = resolve(here, '../..')
const schema = JSON.parse(readFileSync(
  resolve(repository, 'config/schema/normalized-graph.schema.json'), 'utf8'))
const fixture = JSON.parse(readFileSync(
  resolve(repository, 'tests/fixtures/normalized/network-observability.json'), 'utf8'))
const validate = new Ajv2020({ allErrors: true, strictRequired: false,
  strictTypes: false }).compile(schema)

test('normalized configuration fixture satisfies its strict schema', () => {
  assert.equal(validate(fixture), true, JSON.stringify(validate.errors))

  const extraTopLevel = structuredClone(fixture)
  extraTopLevel.credential = 'must-not-be-permitted'
  assert.equal(validate(extraTopLevel), false, 'unknown top-level property was accepted')

  const extraNested = structuredClone(fixture)
  extraNested.network.faults[0].unbounded = true
  assert.equal(validate(extraNested), false, 'unknown nested property was accepted')

  const unknownNetworkField = structuredClone(fixture)
  unknownNetworkField.network.networks[0].driver = 'bridge'
  assert.equal(validate(unknownNetworkField), false,
    'unknown network field was accepted')
})

test('all checked-in configurations normalize to the schema', {
  skip: !process.env.NORMALIZED_CONFIG_CLI,
}, () => {
  const configurations = [resolve(repository, 'graphx.yaml')]
  const visit = directory => {
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
      const path = resolve(directory, entry.name)
      if (entry.isDirectory()) visit(path)
      else if (entry.isFile() && entry.name === 'graphx.yaml') configurations.push(path)
    }
  }
  visit(resolve(repository, 'examples'))
  for (const configuration of configurations) {
    const normalized = JSON.parse(execFileSync(process.env.NORMALIZED_CONFIG_CLI,
      ['config', 'normalize', configuration], { encoding: 'utf8' }))
    assert.equal(validate(normalized), true,
      `${configuration}: ${JSON.stringify(validate.errors)}`)
  }
})


test('resolved instance and per-source settings survive normalization and enforce selection', {
  skip: !process.env.NORMALIZED_CONFIG_CLI,
}, () => {
  const configuration = resolve(repository, 'examples/sdr-node/two-source/graphx.yaml')
  const normalize = instance => JSON.parse(execFileSync(process.env.NORMALIZED_CONFIG_CLI,
    ['config', 'normalize', configuration, '--set', `deployment.instance_id=${instance}`],
    { encoding: 'utf8', env: { ...process.env, GRAPHX_OVERRIDES: '' } }))
  const a = normalize('lab-a')
  const b = normalize('lab-b')
  assert.equal(validate(a), true, JSON.stringify(validate.errors))
  assert.equal(validate(b), true, JSON.stringify(validate.errors))
  assert.deepEqual(a.graph, b.graph)
  assert.equal(b.deployment.instance_id, 'lab-b')
  const node = selectNormalizedNode(a, { instanceId: 'lab-a', nodeId: 'sdr-east' })
  assert.equal(node.sdr.frequency_hz, 100000000)
  assert.equal(node.sdr.credentials.private_key_file, '/run/sdr-east/server.key')
  assert.throws(() => selectNormalizedNode(a, { instanceId: 'lab-b', nodeId: node.id }), /mismatch/)
  assert.throws(() => selectNormalizedNode(a, { instanceId: 'lab-a', nodeId: 'missing' }), /unknown/)
  const mutations = [
    c => { delete c.deployment.instance_id },
    c => { c.deployment.instance_id = 'lab-a\n' },
    c => { c.graph.nodes[0].sdr = null },
    c => { c.graph.nodes[0].sdr.frequency_hz = true },
    c => { c.graph.nodes[0].sdr.frequency_hz = 6000000001 },
    c => { c.graph.nodes[0].sdr.sample_interval_ms = 0 },
    c => { c.graph.nodes[0].sdr.credentials.private_key = 'inline-secret' },
    c => { delete c.graph.nodes[0].sdr.credentials.ca_file },
    c => { c.graph.nodes[0].sdr.credentials.server_name = 'sdr-east\n' },
    c => { c.graph.nodes[0].sdr.samples_edge = 'missing' },
    c => { c.graph.nodes[0].sdr.samples_edge = 'samples-west' },
    c => { c.graph.edges[0].to.node = 'missing' },
    c => { c.graph.edges[1].transport.tls.enabled = false },
    c => { c.graph.nodes.push(structuredClone(c.graph.nodes[0])) },
  ]
  for (const mutate of mutations) {
    const candidate = structuredClone(a)
    mutate(candidate)
    assert.throws(() => selectNormalizedNode(candidate, { instanceId: 'lab-a', nodeId: node.id }))
  }
  const malformed = structuredClone(a)
  malformed.deployment.instance_id = 'lab-a\n'
  assert.equal(validate(malformed), false)
  const missing = structuredClone(a)
  delete missing.deployment.instance_id
  assert.equal(validate(missing), false)
})
