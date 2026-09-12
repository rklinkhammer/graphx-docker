import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
import { MAX_NORMALIZED_CONFIG_BYTES, loadNormalizedConfig,
  loadTelemetryConfiguration } from './normalized-config.mjs'

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const fixturePath = resolve(repository, 'tests/fixtures/normalized/network-observability.json')
const fixture = JSON.parse(readFileSync(fixturePath, 'utf8'))

function temporaryFile(contents) {
  const directory = mkdtempSync(resolve(tmpdir(), 'graphx-normalized-config-'))
  const path = resolve(directory, 'normalized.json')
  writeFileSync(path, contents)
  return { path, cleanup: () => rmSync(directory, { recursive: true }) }
}

test('loads normalized JSON without a second configuration representation', () => {
  const loaded = loadTelemetryConfiguration({
    environment: { GRAPHX_NORMALIZED_CONFIG: fixturePath }, fallbackPath: '/not-used.yaml',
  })
  assert.deepEqual(loaded.config, fixture)
})

test('rejects malformed, oversized, incompatible, and unsafe normalized files', t => {
  const malformed = temporaryFile('{')
  t.after(malformed.cleanup)
  assert.throws(() => loadNormalizedConfig(malformed.path), /malformed JSON/)

  const oversized = temporaryFile(Buffer.alloc(MAX_NORMALIZED_CONFIG_BYTES + 1))
  t.after(oversized.cleanup)
  assert.throws(() => loadNormalizedConfig(oversized.path), /exceeds the .*byte limit/)

  const incompatible = structuredClone(fixture)
  incompatible.contract_version = 2
  const wrongVersion = temporaryFile(JSON.stringify(incompatible))
  t.after(wrongVersion.cleanup)
  assert.throws(() => loadNormalizedConfig(wrongVersion.path), /contract_version must be 1/)

  const directory = mkdtempSync(resolve(tmpdir(), 'graphx-normalized-symlink-'))
  t.after(() => rmSync(directory, { recursive: true }))
  const target = resolve(directory, 'target.json')
  const link = resolve(directory, 'link.json')
  writeFileSync(target, JSON.stringify(fixture))
  symlinkSync(target, link)
  assert.throws(() => loadNormalizedConfig(link), /cannot read/)
})

test('enforces graph and network collection bounds', t => {
  const tooManyNodes = structuredClone(fixture)
  tooManyNodes.graph.nodes = Array.from({ length: 1025 }, (_, index) => ({
    ...fixture.graph.nodes[0], id: `node-${index}`,
  }))
  const nodes = temporaryFile(JSON.stringify(tooManyNodes))
  t.after(nodes.cleanup)
  assert.throws(() => loadNormalizedConfig(nodes.path), /graph.nodes exceeds 1024 entries/)

  const invalidNetwork = structuredClone(fixture)
  delete invalidNetwork.network.edge_paths
  const network = temporaryFile(JSON.stringify(invalidNetwork))
  t.after(network.cleanup)
  assert.throws(() => loadNormalizedConfig(network.path), /network.edge_paths must be an array/)
})

test('requires the authoritative normalized configuration contract', () => {
  assert.throws(() => loadTelemetryConfiguration({ environment: {} }),
    /GRAPHX_NORMALIZED_CONFIG is required/)
})
