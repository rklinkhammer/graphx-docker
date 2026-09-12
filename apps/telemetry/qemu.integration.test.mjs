import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import dgram from 'node:dgram'
import { once } from 'node:events'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { createServer } from 'node:net'
import { dirname, resolve } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'
import { signEnvelope } from './security.mjs'
import { normalizedConfigEnvironment } from './test-config.mjs'

const here = dirname(fileURLToPath(import.meta.url))
const repository = resolve(here, '../..')
const secret = '0123456789abcdef0123456789abcdef'

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

test('QEMU packet observations remain raw and drive the shared topology', { timeout: 10000 }, async () => {
  const temporary = await mkdtemp(resolve(tmpdir(), 'graphx-qemu-evidence-'))
  const evidence = resolve(temporary, 'accelerator-evidence.json')
  const runtimeEvidence = { state: 'booting', vmState: 'running', guestState: 'probing',
    guestProtocols: { tcp: false, udp: false }, requestedAccelerator: 'auto',
    selectedAccelerator: 'tcg', actualAccelerator: 'tcg',
    evidenceSource: 'QMP query-status + query-kvm', qmpStatus: { running: true },
    kvm: { present: false, enabled: false }, updatedAt: Date.now() }
  await writeFile(evidence, JSON.stringify(runtimeEvidence))
  const port = await availablePort()
  const udpPort = await availablePort()
  const child = spawn(process.execPath, ['server.mjs'], {
    cwd: here,
    env: { ...process.env, PORT: String(port), GRAPHX_TELEMETRY_PORT: String(udpPort),
      GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
      ...normalizedConfigEnvironment(resolve(repository, 'examples/qemu-node/external/graphx.yaml')),
      GRAPHX_TELEMETRY_SHARED_SECRET: secret, GRAPHX_CONTROL_TOKEN: secret.split('').reverse().join(''),
      GRAPHX_CAPTURE_ENABLED: 'false', GRAPHX_HISTORY_ENABLED: 'false',
      GRAPHX_QEMU_ACCEL: 'tcg', GRAPHX_QEMU_REQUESTED_ACCEL: 'auto',
      GRAPHX_QEMU_EVIDENCE_FILE: evidence,
      GRAPHX_PACKET_HISTORY_URL: '' },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  try {
    await waitFor(`http://127.0.0.1:${port}/api/health`)
    const initial = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(initial.topology.nodes.find(node => node.id === 'qemu-node').execution, 'host')
    assert.equal(initial.topology.nodes.find(node => node.id === 'qemu-node').runtime, 'qemu')
    assert.equal(initial.topology.nodes.find(node => node.id === 'qemu-node').actualAccelerator, 'tcg')
    assert.equal(initial.nodes['qemu-node'].status, 'booting')
    assert.equal(initial.topology.nodes.find(node => node.id === 'qemu-node').vmState, 'running')
    assert.equal(initial.topology.nodes.find(node => node.id === 'qemu-node').guestState, 'probing')
    assert.ok(initial.topology.networkNodes.some(node => node.id === 'qemu-node-host-runtime'))
    assert.ok(initial.topology.networkNodes.some(node => node.id === 'qemu-node-guest-app'))
    assert.equal(initial.topology.networkNodes.find(node => node.id === 'qemu-node-host-runtime').status, 'running')
    assert.equal(initial.topology.networkNodes.find(node => node.id === 'qemu-node').status, 'running')
    assert.equal(initial.topology.networkNodes.find(node => node.id === 'qemu-node-guest-app').status, 'probing')
    assert.equal(initial.topology.edges.length, 4)
    assert.ok(initial.topology.edges.every(edge => edge.dataPlane === 'external' && edge.framing === 'none'))
    assert.equal(initial.packetHistory.enabled, false)

    const readyAt = Date.now()
    await writeFile(evidence, JSON.stringify({ ...runtimeEvidence, state: 'ready',
      guestState: 'ready', guestProtocols: { tcp: true, udp: true },
      guestReadinessAt: readyAt, updatedAt: readyAt }))
    const ready = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(ready.nodes['qemu-node'].status, 'ready')
    assert.equal(ready.topology.networkNodes.find(node => node.id === 'qemu-node-guest-app').status, 'ready')
    assert.deepEqual(ready.topology.networkNodes.find(node => node.id === 'qemu-node-guest-app').guestProtocols,
      { tcp: true, udp: true })
    await writeFile(evidence, JSON.stringify({ ...runtimeEvidence, state: 'degraded',
      vmState: 'paused', guestState: 'unavailable', guestProtocols: { tcp: false, udp: false },
      qmpStatus: { running: false, status: 'paused' }, updatedAt: Date.now() }))
    const paused = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(paused.topology.networkNodes.find(node => node.id === 'qemu-node-host-runtime').status, 'running')
    assert.equal(paused.topology.networkNodes.find(node => node.id === 'qemu-node').status, 'paused')
    assert.equal(paused.topology.networkNodes.find(node => node.id === 'qemu-node-guest-app').status, 'unavailable')
    await writeFile(evidence, JSON.stringify({ ...runtimeEvidence, state: 'stopped',
      vmState: 'stopped', guestState: 'stopped', guestProtocols: { tcp: false, udp: false },
      qmpStatus: { running: false, status: 'shutdown' }, updatedAt: Date.now() }))
    const stopped = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(stopped.topology.networkNodes.find(node => node.id === 'qemu-node-host-runtime').status, 'stopped')
    assert.equal(stopped.topology.networkNodes.find(node => node.id === 'qemu-node').status, 'stopped')
    assert.equal(stopped.topology.networkNodes.find(node => node.id === 'qemu-node-guest-app').status, 'stopped')
    await writeFile(evidence, JSON.stringify({ ...runtimeEvidence, state: 'ready',
      guestState: 'ready', guestProtocols: { tcp: true, udp: true },
      guestReadinessAt: readyAt - 60_000, updatedAt: readyAt - 60_000 }))
    const stale = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    assert.equal(stale.nodes['qemu-node'].status, 'degraded')
    await writeFile(evidence, JSON.stringify({ ...runtimeEvidence, state: 'ready',
      guestState: 'ready', guestProtocols: { tcp: true, udp: true },
      guestReadinessAt: Date.now(), updatedAt: Date.now() }))

    const socket = dgram.createSocket('udp4')
    const definitions = [
      ['origin-qemu-tcp', 'qemu-node', 'TCP'],
      ['origin-qemu-udp', 'qemu-node', 'UDP'],
      ['qemu-receiver-tcp', 'host-receiver', 'TCP'],
      ['qemu-receiver-udp', 'host-receiver', 'UDP'],
    ]
    for (const [edgeId, nodeId, protocol] of definitions) {
      const event = { kind: 'network_packet', event: 'receive', nodeId, edgeId,
        timestamp: Date.now(), sequence: 1, wireBytes: 80, payloadBytes: 24, protocol,
        sourceAddress: '10.0.2.2', destinationAddress: '10.0.2.15', sourcePort: 50000,
        destinationPort: protocol === 'TCP' ? 18001 : 19001, direction: 'observed',
        observationSource: 'qemu-pcap', type: `Raw${protocol}` }
      await new Promise((resolveSend, rejectSend) => socket.send(
        Buffer.from(JSON.stringify(signEnvelope(event, secret))), udpPort, '127.0.0.1',
        error => error ? rejectSend(error) : resolveSend()))
    }
    socket.close()
    await new Promise(resolveWait => setTimeout(resolveWait, 100))
    const snapshot = await (await fetch(`http://127.0.0.1:${port}/api/topology`)).json()
    for (const [edgeId] of definitions) {
      assert.equal(snapshot.edges[edgeId].sent, 1)
      assert.equal(snapshot.edges[edgeId].received, 1)
      assert.equal(snapshot.edges[edgeId].metricSources.counters, 'measured')
    }
    assert.equal(snapshot.recent.length, 4)
    assert.ok(snapshot.recent.every(event => event.kind === 'network_packet'))
  } finally {
    child.kill('SIGTERM')
    if (child.exitCode == null) await once(child, 'exit')
    await rm(temporary, { recursive: true, force: true })
  }
})
