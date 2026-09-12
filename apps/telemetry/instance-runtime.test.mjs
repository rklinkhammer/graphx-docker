import assert from 'node:assert/strict'
import { execFileSync, spawn } from 'node:child_process'
import dgram from 'node:dgram'
import { once } from 'node:events'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import test from 'node:test'
import { DatabaseSync } from 'node:sqlite'
import { signEnvelope, verifyEnvelope } from './security.mjs'
import { ControlPlane, controlConfig } from './control.mjs'
import { normalizedConfigEnvironment } from './test-config.mjs'

const repository = resolve(import.meta.dirname, '../..')
const cli = process.env.NORMALIZED_CONFIG_CLI || join(repository, 'build/dev/graphx')
const secret = 'source-runtime-credential-for-instance-testing'
const token = 'operator-credential-for-instance-testing'
const observation = 'observer-credential-for-instance-testing'
const pause = ms => new Promise(resolve => setTimeout(resolve, ms))
async function port() {
  const server = createServer().listen(0, '127.0.0.1')
  await once(server, 'listening')
  const selected = server.address().port
  await new Promise(resolve => server.close(resolve))
  return selected
}
async function until(check) {
  for (let i = 0; i < 150; ++i) {
    try { if (await check()) return } catch { /* startup or bounded propagation */ }
    await pause(20)
  }
  throw new Error('condition did not become true')
}

test('instance registration survives collector restart and rejects stale targets and observations',
  { timeout: 20000 }, async () => {
    const directory = await mkdtemp(join(tmpdir(), 'graphx-instance-runtime-'))
    const source = join(directory, 'graphx.yaml')
    await writeFile(source, (await readFile(join(repository, 'graphx.yaml'), 'utf8'))
      .replace('deployment:', 'deployment:\n  instance_id: lab-a'))
    const normalized = normalizedConfigEnvironment(source)
    const config = JSON.parse(await readFile(normalized.GRAPHX_NORMALIZED_CONFIG, 'utf8'))
    const scope = { graphId: config.graph.id, instanceId: 'lab-a' }
    const manifest = join(directory, 'identities.json')
    const registrations = { version: 2, graph_id: scope.graphId, instance_id: scope.instanceId,
      nodes: config.graph.nodes.map(node => ({ id: node.id, secret_file: `${node.id}.secret` })) }
    for (const node of registrations.nodes)
      await writeFile(join(directory, node.secret_file), node.id === 'generator' ? secret :
        `${node.id}-separate-runtime-secret-for-instance-tests`, { mode: 0o600 })
    await writeFile(manifest, JSON.stringify(registrations), { mode: 0o600 })
    await writeFile(join(directory, 'operator.token'), token, { mode: 0o600 })
    const policy = join(directory, 'policy.json')
    await writeFile(policy, JSON.stringify({ version: 2, graph_id: scope.graphId,
      instance_id: scope.instanceId, principals: [{ id: 'operator', token_file: 'operator.token',
        permissions: ['pause', 'resume', 'commands:read:any'], nodes: ['generator'] }] }), { mode: 0o600 })
    const activation = (action, execution = '') => execFileSync(cli, ['runtime', action, source,
      '--identity-file', manifest, '--node', 'generator',
      ...(execution ? ['--execution-id', execution] : [])],
    { encoding: 'utf8', env: { ...process.env, GRAPHX_OVERRIDES: '' } }).trim()
    const first = activation('activate')
    const httpPort = await port(), udpPort = await port()
    const base = `http://127.0.0.1:${httpPort}`
    const runtime = dgram.createSocket('udp4')
    await once(runtime.bind(0, '127.0.0.1'), 'listening')
    const received = []
    runtime.on('message', data => received.push(verifyEnvelope(JSON.parse(data), secret)))
    const send = (executionId, overrides = {}) => runtime.send(Buffer.from(JSON.stringify(signEnvelope({
      ...scope, executionId, kind: 'trace', event: 'heartbeat', nodeId: 'generator', timestamp: Date.now(),
      ...overrides }, secret))), udpPort, '127.0.0.1')
    const snapshot = async () => (await fetch(`${base}/api/topology`, {
      headers: { authorization: `Bearer ${observation}` } })).json()
    const issue = async (execution, idempotency = execution, other = {}) => fetch(`${base}/api/control/commands`, {
      method: 'POST', headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json',
        'idempotency-key': idempotency }, body: JSON.stringify({ action: 'pause', targetNodes: ['generator'],
        ...scope, targetExecutions: { generator: execution }, ...other }) })
    let child, output = ''
    async function start() {
      child = spawn(process.execPath, ['server.mjs'], { cwd: import.meta.dirname,
        env: { ...process.env, ...normalized, PORT: String(httpPort), GRAPHX_TELEMETRY_PORT: String(udpPort),
          GRAPHX_HTTP_BIND: '127.0.0.1', GRAPHX_TELEMETRY_BIND: '127.0.0.1',
          GRAPHX_RUNTIME_IDENTITY_FILE: manifest, GRAPHX_CONTROL_POLICY_FILE: policy,
          GRAPHX_OBSERVATION_TOKEN: observation, GRAPHX_TELEMETRY_SHARED_SECRET: '',
          GRAPHX_TELEMETRY_SHARED_SECRET_FILE: '', GRAPHX_CONTROL_TOKEN: '', GRAPHX_CONTROL_TOKEN_FILE: '',
          GRAPHX_HISTORY_ENABLED: 'true', GRAPHX_HISTORY_DATABASE_FILE: join(directory, 'history.sqlite'),
          GRAPHX_CAPTURE_ENABLED: 'false' }, stdio: ['ignore', 'pipe', 'pipe'] })
      child.stderr.on('data', data => { output += data })
      child.stdout.on('data', data => { output += data })
      await until(async () => (await fetch(`${base}/api/live`)).ok)
    }
    async function stop() {
      if (child && child.exitCode == null) { child.kill('SIGTERM'); await once(child, 'exit') }
    }
    try {
      await start()
      send(first, { instanceId: 'lab-b' })
      await pause(80)
      assert.equal((await snapshot()).nodes.generator.lastSeen, null)
      send(first)
      await until(async () => (await snapshot()).nodes.generator.executionId === first)
      assert.equal((await issue(first, 'cross-instance', { instanceId: 'lab-b' })).status, 409)
      const accepted = await issue(first, 'restart-key')
      assert.equal(accepted.status, 202, await accepted.clone().text())
      const command = (await accepted.json()).command
      await until(() => received.some(value => value?.commandId === command.id))
      assert.equal(received[0].executionId, first)
      activation('retire', first)
      const second = activation('activate')
      assert.notEqual(second, first)
      send(first)
      await pause(60)
      assert.notEqual((await snapshot()).nodes.generator.executionId, first)
      send(second)
      await until(async () => (await snapshot()).nodes.generator.executionId === second)
      assert.equal((await issue(first, 'stale-target')).status, 409)
      assert.equal((await issue(second, 'restart-key')).status, 409)
      const status = await (await fetch(`${base}/api/control/commands/${command.id}`, {
        headers: { authorization: `Bearer ${token}` } })).json()
      assert.equal(status.status, 'rejected')
      await stop()
      const database = new DatabaseSync(join(directory, config.deployment.resource_key, 'history.sqlite'),
        { readOnly: true })
      try {
        assert.equal(database.prepare("SELECT value FROM history_metadata WHERE key='graph_id'").get().value,
          JSON.stringify([scope.graphId, scope.instanceId]))
        const records = database.prepare("SELECT data_json FROM history_records WHERE kind='trace'").all()
          .map(row => JSON.parse(row.data_json))
        assert.ok(records.some(event => event.executionId === first && event.instanceId === scope.instanceId))
        assert.ok(records.some(event => event.executionId === second))
      } finally { database.close() }
      await start()
      send(first)
      await pause(70)
      assert.equal((await snapshot()).nodes.generator.lastSeen, null)
      send(second)
      await until(async () => (await snapshot()).nodes.generator.executionId === second)
      assert.equal((await issue(second, 'after-reconnect')).status, 202)
    } catch (error) { error.message += `\ncollector output:\n${output}`; throw error }
    finally { await stop(); runtime.close(); await rm(directory, { recursive: true, force: true }) }
  })

test('execution-bound acknowledgement and idempotency refuse replacement execution', () => {
  const scope = { graphId: 'graph', instanceId: 'lab-a' }
  const plane = new ControlPlane(controlConfig(), new Set(['source']), { scope })
  const first = 'a'.repeat(32), second = 'b'.repeat(32)
  const intent = { action: 'pause', targetNodes: ['source'], actor: 'operator',
    targetExecutions: { source: first }, idempotencyKey: 'same-request' }
  const command = plane.issue(intent, () => 1).command
  const acknowledgement = { ...scope, kind: 'control_ack', action: 'pause', nodeId: 'source',
    commandId: command.id, accepted: true, executionId: second }
  assert.equal(plane.acknowledge(acknowledgement), false)
  assert.equal(plane.acknowledge({ ...acknowledgement, executionId: first, instanceId: 'lab-b' }), false)
  assert.throws(() => plane.issue({ ...intent, targetExecutions: { source: second } }, () => 1), /different command/)
  assert.equal(plane.acknowledge({ ...acknowledgement, executionId: first }), true)
  assert.deepEqual(plane.auditRecords()[0].targetExecutions, { source: first })
})
