import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import dgram from 'node:dgram'
import { once } from 'node:events'
import { mkdtemp, rename, rm, writeFile } from 'node:fs/promises'
import { createServer as createHttpServer } from 'node:http'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'
import { WebSocket } from 'ws'
import { ReplayCache, signEnvelope, verifyEnvelope } from './security.mjs'

const sharedSecret = 'phase-8-telemetry-secret-0123456789'
const transformSecret = 'phase-8-transform-secret-012345678901'
const sinkSecret = 'phase-8-sink-secret-01234567890123456'
const sourceToken = 'phase-8-source-operator-token-0123456789'
const adminToken = 'phase-8-admin-operator-token-01234567890'
const observationToken = 'phase-8-observation-token-0123456789012'

async function availablePort() {
  const server = createServer().listen(0, '127.0.0.1')
  await once(server, 'listening')
  const { port } = server.address()
  server.close()
  await once(server, 'close')
  return port
}

async function waitForReady(base) {
  for (let attempt = 0; attempt < 100; ++attempt) {
    try { if ((await fetch(`${base}/api/ready`)).ok) return }
    catch { /* Process is still starting. */ }
    await new Promise(resolveWait => setTimeout(resolveWait, 25))
  }
  throw new Error('telemetry service did not become ready')
}

async function waitForCommand(base, commandId, token, expected) {
  for (let attempt = 0; attempt < 100; ++attempt) {
    const response = await fetch(`${base}/api/control/commands/${commandId}`,
      { headers: { authorization: `Bearer ${token}` } })
    if (response.ok) {
      const command = await response.json()
      if (command.status === expected) return command
    }
    await new Promise(resolveWait => setTimeout(resolveWait, 20))
  }
  throw new Error(`control command did not become ${expected}`)
}

function headers(token, idempotencyKey = null) {
  return { authorization: `Bearer ${token}`, 'content-type': 'application/json',
    ...(idempotencyKey ? { 'idempotency-key': idempotencyKey } : {}) }
}

test('authorized control API enforces scope, correlation, idempotency, timeout, and audit',
  { timeout: 15000 }, async () => {
    const directory = await mkdtemp(join(tmpdir(), 'graphx-phase8-'))
    const moduleDirectory = dirname(fileURLToPath(import.meta.url))
    const policyPath = join(directory, 'policy.json')
    const httpPort = await availablePort()
    const udpPort = await availablePort()
    const runtime = dgram.createSocket('udp4')
    const impostor = dgram.createSocket('udp4')
    let acknowledge = true
    let impostorCommands = 0
    const replay = new ReplayCache()
    await writeFile(join(directory, 'source.token'), `${sourceToken}\n`, { mode: 0o600 })
    await writeFile(join(directory, 'admin.token'), `${adminToken}\n`, { mode: 0o600 })
    await writeFile(join(directory, 'generator.secret'), `${sharedSecret}\n`, { mode: 0o600 })
    await writeFile(join(directory, 'transform.secret'), `${transformSecret}\n`, { mode: 0o600 })
    await writeFile(join(directory, 'sink.secret'), `${sinkSecret}\n`, { mode: 0o600 })
    const policy = { version: 1, principals: [
      { id: 'source-operator', token_file: 'source.token', permissions: ['pause', 'resume'],
        nodes: ['generator'] },
      { id: 'control-admin', token_file: 'admin.token',
        permissions: ['pause', 'resume', 'reset', 'commands:read:any', 'audit:read'],
        nodes: ['*'] },
    ] }
    await writeFile(policyPath, JSON.stringify(policy))
    const identitiesPath = join(directory, 'identities.json')
    await writeFile(identitiesPath, JSON.stringify({ version: 1, nodes: [
      { id: 'generator', secret_file: 'generator.secret' },
      { id: 'transform', secret_file: 'transform.secret' },
      { id: 'sink', secret_file: 'sink.secret' },
    ] }))
    const child = spawn(process.execPath, ['server.mjs'], { cwd: moduleDirectory,
      env: { ...process.env, PORT: String(httpPort), GRAPHX_TELEMETRY_PORT: String(udpPort),
        GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
        GRAPHX_CONFIG: resolve(moduleDirectory, '../../graphx.yaml'),
        GRAPHX_CONTROL_POLICY_FILE: policyPath, GRAPHX_RUNTIME_IDENTITY_FILE: identitiesPath,
        GRAPHX_OBSERVATION_TOKEN: observationToken, GRAPHX_HISTORY_ENABLED: 'true',
        GRAPHX_HISTORY_DATABASE_FILE: join(directory, 'history.sqlite'),
        GRAPHX_HISTORY_BATCH_SIZE: '1', GRAPHX_HISTORY_FLUSH_INTERVAL_MS: '10' },
      stdio: ['ignore', 'pipe', 'pipe'] })
    const base = `http://127.0.0.1:${httpPort}`
    try {
      await once(runtime.bind(0, '127.0.0.1'), 'listening')
      await once(impostor.bind(0, '127.0.0.1'), 'listening')
      impostor.on('message', () => { impostorCommands++ })
      runtime.on('message', data => {
        const command = verifyEnvelope(JSON.parse(data), sharedSecret, replay)
        if (!acknowledge || command?.kind !== 'control') return
        const ack = { kind: 'control_ack', nodeId: command.targetNode, action: command.action,
          commandId: command.commandId, accepted: true,
          state: command.action === 'pause' ? 'paused' : 'running' }
        runtime.send(JSON.stringify(signEnvelope(ack, sharedSecret)), udpPort, '127.0.0.1')
      })
      await waitForReady(base)
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'heartbeat',
        nodeId: 'generator', timestamp: Date.now(), cpuPercent: 1 }, sharedSecret)),
      udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 50))
      impostor.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'heartbeat',
        nodeId: 'generator', timestamp: Date.now(), cpuPercent: 1 }, transformSecret)),
      udpPort, '127.0.0.1')

      const unauthenticated = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: { 'content-type': 'application/json' }, body: JSON.stringify({ action: 'pause' }) })
      assert.equal(unauthenticated.status, 401)
      const denied = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(sourceToken), body: JSON.stringify({ action: 'reset' }) })
      assert.equal(denied.status, 403)

      const firstResponse = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(sourceToken, 'phase8-pause-1'),
        body: JSON.stringify({ action: 'pause', targetNodes: ['generator'], reason: 'test pause' }) })
      assert.equal(firstResponse.status, 202)
      const first = await firstResponse.json()
      assert.match(first.command.id, /^[0-9a-f-]{36}$/)
      await new Promise(resolveWait => setTimeout(resolveWait, 50))
      const accepted = await (await fetch(`${base}/api/control/commands/${first.command.id}`,
        { headers: { authorization: `Bearer ${sourceToken}` } })).json()
      assert.equal(accepted.status, 'accepted')
      assert.equal(accepted.acknowledgements.generator.state, 'paused')
      assert.equal(impostorCommands, 0)

      const replayResponse = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(sourceToken, 'phase8-pause-1'),
        body: JSON.stringify({ action: 'pause', targetNodes: ['generator'], reason: 'test pause' }) })
      const repeated = await replayResponse.json()
      assert.equal(repeated.replayed, true)
      assert.equal(repeated.command.id, first.command.id)
      const conflict = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(sourceToken, 'phase8-pause-1'),
        body: JSON.stringify({ action: 'resume', targetNodes: ['generator'], reason: 'test pause' }) })
      assert.equal(conflict.status, 409)

      acknowledge = false
      const pendingResponse = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(sourceToken, 'phase8-resume-1'),
        body: JSON.stringify({ action: 'resume', targetNodes: ['generator'] }) })
      const pending = await pendingResponse.json()
      impostor.send(JSON.stringify(signEnvelope({ kind: 'control_ack', nodeId: 'generator',
        action: 'resume', commandId: pending.command.id, accepted: true, state: 'running' },
      transformSecret)), udpPort, '127.0.0.1')
      runtime.send(JSON.stringify(signEnvelope({ kind: 'control_ack', nodeId: 'transform',
        action: 'resume', commandId: pending.command.id, accepted: true, state: 'running' },
      sharedSecret)), udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 50))
      const stillPending = await (await fetch(`${base}/api/control/commands/${pending.command.id}`,
        { headers: { authorization: `Bearer ${sourceToken}` } })).json()
      assert.equal(stillPending.status, 'pending')
      await new Promise(resolveWait => setTimeout(resolveWait, 2050))
      const timedOut = await (await fetch(`${base}/api/control/commands/${pending.command.id}`,
        { headers: { authorization: `Bearer ${sourceToken}` } })).json()
      assert.equal(timedOut.status, 'timed-out')

      const auditDenied = await fetch(`${base}/api/control/audit`,
        { headers: { authorization: `Bearer ${sourceToken}` } })
      assert.equal(auditDenied.status, 403)
      const audit = await (await fetch(`${base}/api/control/audit`,
        { headers: { authorization: `Bearer ${adminToken}` } })).json()
      assert.ok(audit.records.some(record => record.decision === 'denied'))
      assert.ok(audit.records.some(record => record.decision === 'timed-out'))
      assert.equal(JSON.stringify(audit).includes(sourceToken), false)
      for (const credential of [sourceToken, adminToken, observationToken, sharedSecret]) {
        const secretReason = await fetch(`${base}/api/control/commands`, { method: 'POST',
          headers: headers(sourceToken),
          body: JSON.stringify({ action: 'pause', reason: `do-not-store:${credential}` }) })
        assert.equal(secretReason.status, 400)
      }
      await new Promise(resolveWait => setTimeout(resolveWait, 50))
      const observedHistory = await (await fetch(`${base}/api/history?kind=control_audit`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      assert.deepEqual(observedHistory.records, [])
      assert.equal((await fetch(`${base}/api/control/audit/history`,
        { headers: { authorization: `Bearer ${sourceToken}` } })).status, 403)
      const durableAudit = await (await fetch(`${base}/api/control/audit/history`,
        { headers: { authorization: `Bearer ${adminToken}` } })).json()
      assert.ok(durableAudit.records.some(record => record.kind === 'control_audit'))
      const observedSnapshot = await (await fetch(`${base}/api/topology`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      assert.equal('commands' in observedSnapshot.control, false)
      assert.equal(child.exitCode, null)
    } finally {
      runtime.close()
      impostor.close()
      child.kill('SIGTERM')
      if (child.exitCode == null) await once(child, 'exit')
      await rm(directory, { recursive: true, force: true })
    }
  })

test('runtime rejection cannot expose configured credentials in commands, audit, history, or logs',
  { timeout: 15000 }, async () => {
    const directory = await mkdtemp(join(tmpdir(), 'graphx-phase8-redaction-'))
    const moduleDirectory = dirname(fileURLToPath(import.meta.url))
    const httpPort = await availablePort()
    const udpPort = await availablePort()
    const policyPath = join(directory, 'policy.json')
    const identityPath = join(directory, 'identities.json')
    const historyPath = join(directory, 'history.sqlite')
    const runtime = dgram.createSocket('udp4')
    const logs = []
    let nextError = null
    let child = null
    const credentials = [sourceToken, adminToken, observationToken, sharedSecret,
      transformSecret, sinkSecret]
    const environment = { ...process.env, PORT: String(httpPort),
      GRAPHX_TELEMETRY_PORT: String(udpPort), GRAPHX_HTTP_BIND: '127.0.0.1',
      GRAPHX_TELEMETRY_BIND: '127.0.0.1',
      GRAPHX_CONFIG: resolve(moduleDirectory, '../../graphx.yaml'),
      GRAPHX_CONTROL_POLICY_FILE: policyPath, GRAPHX_RUNTIME_IDENTITY_FILE: identityPath,
      GRAPHX_OBSERVATION_TOKEN: observationToken, GRAPHX_HISTORY_ENABLED: 'true',
      GRAPHX_HISTORY_DATABASE_FILE: historyPath, GRAPHX_HISTORY_BATCH_SIZE: '1',
      GRAPHX_HISTORY_FLUSH_INTERVAL_MS: '10' }
    const startCollector = () => {
      const collector = spawn(process.execPath, ['server.mjs'], { cwd: moduleDirectory,
        env: environment, stdio: ['ignore', 'pipe', 'pipe'] })
      collector.stdout.on('data', data => logs.push(data.toString()))
      collector.stderr.on('data', data => logs.push(data.toString()))
      return collector
    }
    const stopCollector = async () => {
      if (child?.exitCode == null) {
        child.kill('SIGTERM')
        await once(child, 'exit')
      }
    }
    try {
      await writeFile(join(directory, 'source.token'), `${sourceToken}\n`, { mode: 0o600 })
      await writeFile(join(directory, 'admin.token'), `${adminToken}\n`, { mode: 0o600 })
      await writeFile(join(directory, 'generator.secret'), `${sharedSecret}\n`, { mode: 0o600 })
      await writeFile(join(directory, 'transform.secret'), `${transformSecret}\n`, { mode: 0o600 })
      await writeFile(join(directory, 'sink.secret'), `${sinkSecret}\n`, { mode: 0o600 })
      await writeFile(policyPath, JSON.stringify({ version: 1, principals: [
        { id: 'source-operator', token_file: 'source.token', permissions: ['pause', 'resume'],
          nodes: ['generator'] },
        { id: 'control-admin', token_file: 'admin.token',
          permissions: ['commands:read:any', 'audit:read'], nodes: ['*'] },
      ] }))
      await writeFile(identityPath, JSON.stringify({ version: 1, nodes: [
        { id: 'generator', secret_file: 'generator.secret' },
        { id: 'transform', secret_file: 'transform.secret' },
        { id: 'sink', secret_file: 'sink.secret' },
      ] }))
      await once(runtime.bind(0, '127.0.0.1'), 'listening')
      runtime.on('message', data => {
        const command = verifyEnvelope(JSON.parse(data), sharedSecret, new ReplayCache())
        if (command?.kind !== 'control' || nextError == null) return
        const acknowledgement = { kind: 'control_ack', nodeId: 'generator',
          action: command.action, commandId: command.commandId, accepted: false,
          error: nextError }
        runtime.send(JSON.stringify(signEnvelope(acknowledgement, sharedSecret)),
          udpPort, '127.0.0.1')
      })
      child = startCollector()
      const base = `http://127.0.0.1:${httpPort}`
      await waitForReady(base)
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'heartbeat',
        nodeId: 'generator', timestamp: Date.now(), cpuPercent: 1 }, sharedSecret)),
      udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 50))

      for (const [index, credential] of credentials.entries()) {
        runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'error',
          nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(),
          message: `diagnostic:${credential}` }, sharedSecret)), udpPort, '127.0.0.1')
        runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'receive',
          nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), sequence: index + 1,
          messageId: credential, parentMessageId: `parent:${credential}`,
          type: `sample:${credential}`, unexpected: credential }, sharedSecret)),
        udpPort, '127.0.0.1')
        runtime.send(JSON.stringify(signEnvelope({ kind: 'capture', event: 'frame',
          nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), sequence: index + 1,
          messageId: credential, traceId: `trace:${credential}`, direction: 'received',
          captureFile: 'generator.pcapng', capturePacket: index, captureOffset: index },
        sharedSecret)), udpPort, '127.0.0.1')
        runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'connection',
          nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), message: credential },
        sharedSecret)), udpPort, '127.0.0.1')
      }
      await new Promise(resolveWait => setTimeout(resolveWait, 100))

      for (const [index, credential] of credentials.entries()) {
        nextError = index % 2 === 0 ? credential : `runtime failure: ${credential}`
        const response = await fetch(`${base}/api/control/commands`, { method: 'POST',
          headers: headers(sourceToken, `credential-rejection-${index}`),
          body: JSON.stringify({ action: index % 2 === 0 ? 'pause' : 'resume',
            targetNodes: ['generator'] }) })
        assert.equal(response.status, 202)
        const issued = await response.json()
        const rejected = await waitForCommand(base, issued.command.id, sourceToken, 'rejected')
        assert.equal(rejected.acknowledgements.generator.error, 'runtime-rejected')
        for (const secretValue of credentials)
          assert.equal(JSON.stringify(rejected).includes(secretValue), false)
      }

      const liveAudit = await (await fetch(`${base}/api/control/audit`,
        { headers: { authorization: `Bearer ${adminToken}` } })).json()
      const observed = await (await fetch(`${base}/api/topology`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      const telemetryHistory = await (await fetch(`${base}/api/history?limit=100`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      assert.equal('commands' in observed.control, false)
      for (const credential of credentials)
        assert.equal(JSON.stringify({ liveAudit, observed, telemetryHistory }).includes(credential), false)

      await stopCollector()
      child = startCollector()
      await waitForReady(base)
      let durableAudit = null
      for (let attempt = 0; attempt < 100; ++attempt) {
        const response = await fetch(`${base}/api/control/audit/history`,
          { headers: { authorization: `Bearer ${adminToken}` } })
        if (response.ok) {
          durableAudit = await response.json()
          if (durableAudit.records.length >= credentials.length) break
        }
        await new Promise(resolveWait => setTimeout(resolveWait, 20))
      }
      assert.ok(durableAudit?.records.length >= credentials.length)
      const reopenedTelemetryHistory = await (await fetch(`${base}/api/history?limit=100`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      assert.ok(reopenedTelemetryHistory.records.length >= credentials.length)
      for (const credential of credentials) {
        assert.equal(JSON.stringify(durableAudit).includes(credential), false)
        assert.equal(JSON.stringify(reopenedTelemetryHistory).includes(credential), false)
        assert.equal(logs.join('').includes(credential), false)
      }
    } finally {
      runtime.close()
      await stopCollector().catch(() => {})
      await rm(directory, { recursive: true, force: true })
    }
  })

test('file-only rotation filters candidate and retired credentials before fan-out',
  { timeout: 15000 }, async () => {
    const directory = await mkdtemp(join(tmpdir(), 'graphx-phase8-rotation-redaction-'))
    const moduleDirectory = dirname(fileURLToPath(import.meta.url))
    const httpPort = await availablePort()
    const udpPort = await availablePort()
    const otlpRequests = []
    const otlpServer = createHttpServer((request, response) => {
      const chunks = []
      request.on('data', chunk => chunks.push(chunk))
      request.on('end', () => {
        otlpRequests.push(Buffer.concat(chunks).toString('utf8'))
        response.writeHead(200, { 'content-type': 'application/json' })
        response.end('{}')
      })
    })
    await once(otlpServer.listen(0, '127.0.0.1'), 'listening')
    const otlpPort = otlpServer.address().port
    const policyPath = join(directory, 'policy.json')
    const identityPath = join(directory, 'identities.json')
    const previousCredentialPath = join(directory, 'previous-credentials.json')
    const historyPath = join(directory, 'history.sqlite')
    const rotatedAdmin = 'phase-8-rotated-admin-token-012345678901'
    const rotatedRuntime = 'phase-8-rotated-generator-secret-012345678'
    const runtime = dgram.createSocket('udp4')
    const logs = []
    let child = null
    let websocket = null
    const environment = { ...process.env, PORT: String(httpPort),
      GRAPHX_TELEMETRY_PORT: String(udpPort), GRAPHX_HTTP_BIND: '127.0.0.1',
      GRAPHX_TELEMETRY_BIND: '127.0.0.1',
      GRAPHX_CONFIG: resolve(moduleDirectory, '../../graphx.yaml'),
      GRAPHX_CONTROL_POLICY_FILE: policyPath, GRAPHX_RUNTIME_IDENTITY_FILE: identityPath,
      GRAPHX_PREVIOUS_CREDENTIALS_FILE: previousCredentialPath,
      GRAPHX_OBSERVATION_TOKEN: observationToken, GRAPHX_HISTORY_ENABLED: 'true',
      GRAPHX_HISTORY_DATABASE_FILE: historyPath, GRAPHX_HISTORY_BATCH_SIZE: '1',
      GRAPHX_HISTORY_FLUSH_INTERVAL_MS: '10',
      GRAPHX_OTLP_ENDPOINT: `http://127.0.0.1:${otlpPort}` }
    const startCollector = () => {
      const collector = spawn(process.execPath, ['server.mjs'], { cwd: moduleDirectory,
        env: environment, stdio: ['ignore', 'pipe', 'pipe'] })
      collector.stdout.on('data', data => logs.push(data.toString()))
      collector.stderr.on('data', data => logs.push(data.toString()))
      return collector
    }
    const stopCollector = async () => {
      if (child?.exitCode == null) {
        child.kill('SIGTERM')
        await once(child, 'exit')
      }
    }
    const replace = async (path, value) => {
      const candidate = `${path}.candidate`
      await writeFile(candidate, value, { mode: 0o600 })
      await rename(candidate, path)
    }
    try {
      await writeFile(join(directory, 'source.token'), sourceToken, { mode: 0o600 })
      await writeFile(join(directory, 'admin.token'), adminToken, { mode: 0o600 })
      await writeFile(join(directory, 'generator.secret'), sharedSecret, { mode: 0o600 })
      await writeFile(join(directory, 'transform.secret'), transformSecret, { mode: 0o600 })
      await writeFile(join(directory, 'sink.secret'), sinkSecret, { mode: 0o600 })
      await writeFile(join(directory, 'previous-admin.token'), adminToken, { mode: 0o600 })
      await writeFile(join(directory, 'previous-generator.secret'), sharedSecret, { mode: 0o600 })
      await writeFile(previousCredentialPath, JSON.stringify({ version: 1,
        expires_at: new Date(0).toISOString(), credentials: [] }), { mode: 0o600 })
      await writeFile(policyPath, JSON.stringify({ version: 1, principals: [
        { id: 'source-operator', token_file: 'source.token', permissions: ['pause', 'resume'],
          nodes: ['generator'] },
        { id: 'control-admin', token_file: 'admin.token',
          permissions: ['reset', 'commands:read:any', 'audit:read'], nodes: ['*'] },
      ] }))
      await writeFile(identityPath, JSON.stringify({ version: 1, nodes: [
        { id: 'generator', secret_file: 'generator.secret' },
        { id: 'transform', secret_file: 'transform.secret' },
        { id: 'sink', secret_file: 'sink.secret' },
      ] }))
      await once(runtime.bind(0, '127.0.0.1'), 'listening')
      child = startCollector()
      const base = `http://127.0.0.1:${httpPort}`
      await waitForReady(base)
      websocket = new WebSocket(`ws://127.0.0.1:${httpPort}/ws`, { headers: {
        'sec-websocket-protocol':
          `graphx-auth.${Buffer.from(observationToken).toString('base64url')}`,
      } })
      const websocketMessages = []
      websocket.on('message', data => websocketMessages.push(data.toString()))
      await once(websocket, 'open')
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'heartbeat',
        nodeId: 'generator', timestamp: Date.now() }, sharedSecret)), udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 25))

      // Project the previous values before switching current files. The same
      // logical role may match during staging, but cross-role reuse still fails
      // closed. The absolute expiry makes the overlap survive process restart.
      await replace(previousCredentialPath, JSON.stringify({ version: 1,
        expires_at: new Date(Date.now() + 59000).toISOString(), credentials: [
          { role: 'control_principal', id: 'control-admin',
            secret_file: 'previous-admin.token' },
          { role: 'runtime_identity', id: 'generator',
            secret_file: 'previous-generator.secret' },
        ] }))

      // Rotate immediately after the heartbeat refresh. The next valid packet
      // must discover and redact the candidate inside the ordinary poll window.
      await replace(join(directory, 'admin.token'), rotatedAdmin)
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'error',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(),
        message: `candidate:${rotatedAdmin}` }, sharedSecret)), udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 25))

      // Rotate the runtime identity and exercise both sides of the transition:
      // an in-flight old-authenticated event containing the candidate, followed
      // by a new-authenticated event containing superseded credentials.
      await replace(join(directory, 'generator.secret'), rotatedRuntime)
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'error',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(),
        message: `candidate:${rotatedRuntime}` }, sharedSecret)), udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 25))
      const correlatedId = `message:${sharedSecret}`
      runtime.send(JSON.stringify(signEnvelope({ kind: 'capture', event: 'frame',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), sequence: 42,
        messageId: correlatedId, traceId: `trace:${adminToken}`, direction: 'received',
        captureFile: 'generator.pcapng', capturePacket: 42, captureOffset: 7 },
      rotatedRuntime)), udpPort, '127.0.0.1')
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'receive',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), sequence: 42,
        messageId: correlatedId, traceId: `trace:${adminToken}`,
        type: `sample:${rotatedAdmin}`, wireBytes: 64 }, rotatedRuntime)),
      udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 150))

      assert.equal((await fetch(`${base}/api/control/audit`,
        { headers: { authorization: `Bearer ${rotatedAdmin}` } })).status, 200)
      const retiredReason = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(rotatedAdmin),
        body: JSON.stringify({ action: 'reset', reason: `retired:${adminToken}` }) })
      assert.equal(retiredReason.status, 400)
      const snapshot = await (await fetch(`${base}/api/topology`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      const history = await (await fetch(`${base}/api/history?limit=100`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      const credentials = [adminToken, rotatedAdmin, sharedSecret, rotatedRuntime]
      for (const credential of credentials)
        assert.equal(JSON.stringify({ snapshot, history, websocketMessages, otlpRequests })
          .includes(credential), false)
      assert.match(JSON.stringify({ snapshot, history }), /\[credential-redacted\]/)

      websocket.close()
      await once(websocket, 'close')
      websocket = null
      await stopCollector()
      child = startCollector()
      await waitForReady(base)
      websocket = new WebSocket(`ws://127.0.0.1:${httpPort}/ws`, { headers: {
        'sec-websocket-protocol':
          `graphx-auth.${Buffer.from(observationToken).toString('base64url')}`,
      } })
      const reopenedWebsocketMessages = []
      websocket.on('message', data => reopenedWebsocketMessages.push(data.toString()))
      await once(websocket, 'open')

      // A retired runtime secret cannot authenticate after restart.
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'error',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(),
        message: 'old-auth-should-not-appear' }, sharedSecret)), udpPort, '127.0.0.1')
      // The new runtime identity is accepted, but old exact values remain
      // redaction-only through snapshot/WebSocket/OTLP/capture/history fan-out.
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'error',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(),
        message: `post-restart:${adminToken}:${sharedSecret}` }, rotatedRuntime)),
      udpPort, '127.0.0.1')
      runtime.send(JSON.stringify(signEnvelope({ kind: 'capture', event: 'frame',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), sequence: 43,
        messageId: `post-restart:${sharedSecret}`, traceId: `trace:${adminToken}`,
        direction: 'received', captureFile: 'generator.pcapng', capturePacket: 43,
        captureOffset: 8 }, rotatedRuntime)), udpPort, '127.0.0.1')
      runtime.send(JSON.stringify(signEnvelope({ kind: 'trace', event: 'receive',
        nodeId: 'generator', edgeId: 'samples', timestamp: Date.now(), sequence: 43,
        messageId: `post-restart:${sharedSecret}`, traceId: `trace:${adminToken}`,
        type: `post-restart:${adminToken}`, wireBytes: 64 }, rotatedRuntime)),
      udpPort, '127.0.0.1')
      await new Promise(resolveWait => setTimeout(resolveWait, 150))

      const retiredAuthentication = await fetch(`${base}/api/control/audit`,
        { headers: { authorization: `Bearer ${adminToken}` } })
      assert.equal(retiredAuthentication.status, 401)
      const restartedReason = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(rotatedAdmin),
        body: JSON.stringify({ action: 'reset', reason: `retired-after-restart:${adminToken}` }) })
      assert.equal(restartedReason.status, 400)
      const reopenedSnapshot = await (await fetch(`${base}/api/topology`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      const reopened = await (await fetch(`${base}/api/history?limit=100`,
        { headers: { authorization: `Bearer ${observationToken}` } })).json()
      const reopenedAudit = await (await fetch(`${base}/api/control/audit`,
        { headers: { authorization: `Bearer ${rotatedAdmin}` } })).json()
      assert.match(JSON.stringify(reopenedSnapshot), /post-restart/)
      assert.match(JSON.stringify(reopened), /post-restart/)
      assert.match(reopenedWebsocketMessages.join(''), /post-restart/)
      assert.match(otlpRequests.join(''), /post-restart/)
      assert.ok(reopenedSnapshot.recent.some(event => event.captures?.length > 0))
      assert.equal(JSON.stringify({ reopenedSnapshot, reopened, reopenedWebsocketMessages,
        otlpRequests }).includes('old-auth-should-not-appear'), false)
      for (const credential of credentials) {
        assert.equal(JSON.stringify({ reopenedSnapshot, reopened, reopenedAudit,
          reopenedWebsocketMessages, otlpRequests }).includes(credential), false)
        assert.equal(logs.join('').includes(credential), false)
      }
      assert.match(JSON.stringify({ reopenedSnapshot, reopened }), /\[credential-redacted\]/)

      await replace(previousCredentialPath, '{broken')
      await new Promise(resolveWait => setTimeout(resolveWait, 1001))
      const invalidTransition = await fetch(`${base}/api/ready`)
      assert.equal(invalidTransition.status, 503)
      assert.equal((await invalidTransition.json()).credentialConfiguration, 'invalid')
      const disabledControl = await fetch(`${base}/api/control/commands`, { method: 'POST',
        headers: headers(rotatedAdmin), body: JSON.stringify({ action: 'reset' }) })
      assert.equal(disabledControl.status, 503)
      assert.match(logs.join(''), /invalid previous credential manifest/)
      for (const credential of credentials) assert.equal(logs.join('').includes(credential), false)

      await replace(previousCredentialPath, JSON.stringify({ version: 1,
        expires_at: new Date(0).toISOString(), credentials: [] }))
      await new Promise(resolveWait => setTimeout(resolveWait, 1001))
      assert.equal((await fetch(`${base}/api/ready`)).status, 200)
    } finally {
      if (websocket?.readyState === WebSocket.OPEN) websocket.close()
      runtime.close()
      await stopCollector().catch(() => {})
      if (otlpServer.listening) {
        const closed = once(otlpServer, 'close')
        otlpServer.close()
        await closed
      }
      await rm(directory, { recursive: true, force: true })
    }
  })

test('collector rejects ambiguous legacy and policy credential models', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-phase8-conflict-'))
  const moduleDirectory = dirname(fileURLToPath(import.meta.url))
  const policyPath = join(directory, 'policy.json')
  await writeFile(policyPath, JSON.stringify({ version: 1, principals: [] }))
  const child = spawn(process.execPath, ['server.mjs'], { cwd: moduleDirectory,
    env: { ...process.env, GRAPHX_CONFIG: resolve(moduleDirectory, '../../graphx.yaml'),
      GRAPHX_CONTROL_POLICY_FILE: policyPath, GRAPHX_CONTROL_TOKEN: sourceToken,
      GRAPHX_RUNTIME_IDENTITY_FILE: policyPath }, stdio: ['ignore', 'ignore', 'pipe'] })
  let error = ''
  child.stderr.on('data', data => { error += data })
  try {
    const [code] = await once(child, 'exit')
    assert.notEqual(code, 0)
    assert.match(error, /mutually exclusive/)
  } finally { if (child.exitCode == null) child.kill('SIGKILL'); await rm(directory, { recursive: true, force: true }) }
})

test('collector fails readiness and control closed on cross-domain credential reuse',
  { timeout: 10000 }, async () => {
    const directory = await mkdtemp(join(tmpdir(), 'graphx-phase8-credential-collision-'))
    const moduleDirectory = dirname(fileURLToPath(import.meta.url))
    const httpPort = await availablePort()
    const udpPort = await availablePort()
    const policyPath = join(directory, 'policy.json')
    const identityPath = join(directory, 'identities.json')
    const reused = 'reused-cross-domain-credential-0123456789012'
    const logs = []
    await writeFile(join(directory, 'admin.token'), reused, { mode: 0o600 })
    await writeFile(join(directory, 'generator.secret'), reused, { mode: 0o600 })
    await writeFile(join(directory, 'transform.secret'), transformSecret, { mode: 0o600 })
    await writeFile(join(directory, 'sink.secret'), sinkSecret, { mode: 0o600 })
    await writeFile(policyPath, JSON.stringify({ version: 1, principals: [
      { id: 'admin', token_file: 'admin.token', permissions: ['reset'], nodes: ['*'] },
    ] }))
    await writeFile(identityPath, JSON.stringify({ version: 1, nodes: [
      { id: 'generator', secret_file: 'generator.secret' },
      { id: 'transform', secret_file: 'transform.secret' },
      { id: 'sink', secret_file: 'sink.secret' },
    ] }))
    const child = spawn(process.execPath, ['server.mjs'], { cwd: moduleDirectory,
      env: { ...process.env, PORT: String(httpPort), GRAPHX_TELEMETRY_PORT: String(udpPort),
        GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
        GRAPHX_CONFIG: resolve(moduleDirectory, '../../graphx.yaml'),
        GRAPHX_CONTROL_POLICY_FILE: policyPath, GRAPHX_RUNTIME_IDENTITY_FILE: identityPath,
        GRAPHX_OBSERVATION_TOKEN: reused }, stdio: ['ignore', 'pipe', 'pipe'] })
    child.stdout.on('data', data => logs.push(data.toString()))
    child.stderr.on('data', data => logs.push(data.toString()))
    const base = `http://127.0.0.1:${httpPort}`
    try {
      let readiness
      for (let attempt = 0; attempt < 100; ++attempt) {
        try { readiness = await fetch(`${base}/api/ready`); break } catch {}
        await new Promise(resolveWait => setTimeout(resolveWait, 25))
      }
      assert.equal(readiness?.status, 503)
      assert.equal((await readiness.json()).credentialConfiguration, 'invalid')
      const reset = await fetch(`${base}/api/control/reset`, { method: 'POST',
        headers: { authorization: `Bearer ${reused}` } })
      assert.equal(reset.status, 503)
      const metrics = await (await fetch(`${base}/metrics`,
        { headers: { authorization: `Bearer ${reused}` } })).text()
      assert.match(metrics, /graphx_control_policy_valid 0/)
      const snapshot = await (await fetch(`${base}/api/topology`,
        { headers: { authorization: `Bearer ${reused}` } })).json()
      assert.equal(snapshot.control.available, false)
      assert.equal(snapshot.control.authenticatedTelemetry, false)
      assert.match(logs.join(''), /credential collision between observation token and control principal 'admin'/)
      assert.equal(logs.join('').includes(reused), false)
    } finally {
      if (child.exitCode == null) child.kill('SIGTERM')
      if (child.exitCode == null) await once(child, 'exit')
      await rm(directory, { recursive: true, force: true })
    }
  })
