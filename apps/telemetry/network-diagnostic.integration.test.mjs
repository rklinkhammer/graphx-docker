import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { createServer } from 'node:net'
import { dirname, resolve } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const repository = resolve(here, '../..')

async function availablePort() {
  const server = createServer()
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const { port } = server.address()
  server.close()
  await once(server, 'close')
  return port
}

async function waitFor(url) {
  for (let attempt = 0; attempt < 80; ++attempt) {
    try {
      const response = await fetch(url)
      if (response.ok) return response
    } catch { /* server is starting */ }
    await new Promise(resolveWait => setTimeout(resolveWait, 25))
  }
  throw new Error(`timed out waiting for ${url}`)
}

test('route-policy evidence drives bounded topology diagnostics', { timeout: 10000 }, async () => {
  const temporary = await mkdtemp(resolve(tmpdir(), 'graphx-route-evidence-'))
  const evidence = resolve(temporary, 'diagnostic-state.json')
  const initial = { version: 1, updatedAt: Date.now(), routeApplied: false, flows: {
    'allowed-flow': { state: 'allowed', evidence: 'receiver-confirmed' },
    'denied-flow': { state: 'policy-denied', evidence: 'nft-counter' },
    'routed-flow': { state: 'missing-route', evidence: 'route-absent' },
  } }
  await writeFile(evidence, JSON.stringify(initial))
  const port = await availablePort()
  const udpPort = await availablePort()
  const child = spawn(process.execPath, ['server.mjs'], { cwd: here,
    env: { ...process.env, PORT: String(port), GRAPHX_TELEMETRY_PORT: String(udpPort),
      GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
      GRAPHX_CONFIG: resolve(repository, 'examples/static-route-policy/graphx.yaml'),
      GRAPHX_NETWORK_DIAGNOSTIC_FILE: evidence, GRAPHX_CAPTURE_ENABLED: 'false',
      GRAPHX_HISTORY_ENABLED: 'false', GRAPHX_TELEMETRY_SHARED_SECRET: '',
      GRAPHX_CONTROL_TOKEN: '' }, stdio: ['ignore', 'pipe', 'pipe'] })
  try {
    await waitFor(`http://127.0.0.1:${port}/api/health`)
    const first = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(first.topology.networkDiagnostic.routeApplied, false)
    assert.equal(first.edges['allowed-flow'].connection, 'allowed')
    assert.equal(first.edges['denied-flow'].diagnosticEvidence, 'nft-counter')
    assert.equal(first.edges['routed-flow'].connection, 'missing-route')
    assert.equal(first.topology.edges.find(edge => edge.id === 'routed-flow').diagnosticState,
      'missing-route')

    await writeFile(evidence, JSON.stringify({ ...initial, updatedAt: Date.now(), routeApplied: true,
      flows: { ...initial.flows,
        'routed-flow': { state: 'route-applied', evidence: 'route-installed' } } }))
    const applied = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(applied.edges['routed-flow'].connection, 'route-applied')
    assert.equal(applied.topology.networkDiagnostic.routeApplied, true)

    await writeFile(evidence, JSON.stringify({ ...initial, flows: {
      ...initial.flows, unexpected: { state: 'allowed', evidence: 'receiver-confirmed' } } }))
    const rejected = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(rejected.topology.networkDiagnostic, undefined)
    await writeFile(evidence, 'x'.repeat(65 * 1024))
    const oversized = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(oversized.topology.networkDiagnostic, undefined)
  } finally {
    child.kill('SIGTERM')
    if (child.exitCode == null) await once(child, 'exit')
    await rm(temporary, { recursive: true, force: true })
  }
})
