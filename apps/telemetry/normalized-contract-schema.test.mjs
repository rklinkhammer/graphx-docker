import assert from 'node:assert/strict'
import { readdirSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
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
