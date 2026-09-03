import assert from 'node:assert/strict'
import { chmodSync, mkdtempSync, renameSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import { ControlAuthorizer, ControlConflictError, ControlPlane, controlAuditHistoryRecord,
  controlConfig, CredentialRegistry, PreviousCredentialStore,
  RuntimeIdentityStore } from './control.mjs'

const nodes = new Set(['generator', 'transform', 'sink'])
const tokenA = 'operator-a-token-01234567890123456789'
const tokenB = 'operator-b-token-01234567890123456789'
const tokenC = 'operator-c-token-01234567890123456789'

function policyFixture() {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-control-'))
  const policy = join(directory, 'policy.json')
  writeFileSync(join(directory, 'source.token'), `${tokenA}\n`, { mode: 0o600 })
  writeFileSync(join(directory, 'admin.token'), `${tokenB}\n`, { mode: 0o600 })
  writeFileSync(policy, JSON.stringify({ version: 1, principals: [
    { id: 'source-operator', token_file: 'source.token', permissions: ['pause', 'resume'],
      nodes: ['generator'] },
    { id: 'admin', token_file: 'admin.token',
      permissions: ['pause', 'resume', 'reset', 'commands:read:any', 'audit:read'],
      nodes: ['*'] },
  ] }))
  return { directory, policy }
}

test('control configuration has strict bounded defaults', () => {
  assert.deepEqual(controlConfig(), { commandTimeoutMs: 2000, commandRetentionSeconds: 3600,
    maxCommands: 1024, maxAuditRecords: 4096, idempotencyTtlSeconds: 3600,
    maxRequestBytes: 4096 })
  assert.throws(() => controlConfig({ surprise: true }), /unknown control/)
  assert.throws(() => controlConfig({ command_timeout_ms: 99 }), /between 100 and 30000/)
})

test('runtime identity manifest requires a distinct secret for every topology node', () => {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-runtime-identities-'))
  try {
    for (const node of nodes) writeFileSync(join(directory, `${node}.secret`),
      `${node}-runtime-secret-01234567890123456789`)
    const manifest = join(directory, 'identities.json')
    writeFileSync(manifest, JSON.stringify({ version: 1, nodes: [...nodes].map(id =>
      ({ id, secret_file: `${id}.secret` })) }))
    const identities = new RuntimeIdentityStore({ manifestFile: manifest, nodeIds: nodes })
    assert.equal(identities.available, true)
    assert.match(identities.secretFor('generator'), /^generator-/)
    assert.notEqual(identities.secretFor('generator'), identities.secretFor('transform'))
    for (const node of nodes)
      assert.equal(identities.containsCredential(`prefix:${identities.secretFor(node)}:suffix`), true)
    writeFileSync(join(directory, 'transform.secret'), 'generator-runtime-secret-01234567890123456789')
    identities.lastCheck = 0
    assert.equal(identities.reload(true), false)
    assert.match(identities.lastError, /must not share/)
  } finally { rmSync(directory, { recursive: true, force: true }) }
})

test('versioned policy authorizes action and node scopes without exposing tokens', () => {
  const fixture = policyFixture()
  try {
    const auth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes })
    const source = auth.authenticate(`Bearer ${tokenA}`)
    assert.equal(source.id, 'source-operator')
    assert.equal(auth.permits(source, 'pause', ['generator']), true)
    assert.equal(auth.permits(source, 'pause', ['transform']), false)
    assert.equal(auth.permits(source, 'reset', ['collector']), false)
    const admin = auth.authenticate(`Bearer ${tokenB}`)
    assert.equal(auth.permits(admin, 'reset', ['collector']), true)
    assert.equal(auth.containsCredential(`prefix:${tokenA}:suffix`), true)
    assert.equal(auth.containsCredential(`prefix:${tokenB}:suffix`), true)
    assert.equal(JSON.stringify(auth.describe(source)).includes(tokenA), false)
    assert.equal(auth.authenticate('Bearer wrong'), null)
  } finally { rmSync(fixture.directory, { recursive: true, force: true }) }
})

test('atomic policy rotation reloads credentials and malformed rotation fails closed', () => {
  const fixture = policyFixture()
  let now = 1000
  try {
    const auth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes, now: () => now })
    assert.ok(auth.authenticate(`Bearer ${tokenA}`))
    writeFileSync(join(fixture.directory, 'source.token'), `${tokenC}\n`, { mode: 0o600 })
    now += 1001
    assert.equal(auth.authenticate(`Bearer ${tokenA}`), null)
    assert.ok(auth.authenticate(`Bearer ${tokenC}`))
    const replacement = join(fixture.directory, 'replacement.json')
    writeFileSync(replacement, JSON.stringify({ version: 1, principals: [
      { id: 'admin', token_file: 'admin.token', permissions: ['reset'], nodes: ['*'] },
    ] }))
    renameSync(replacement, fixture.policy); now += 1001
    assert.equal(auth.authenticate(`Bearer ${tokenA}`), null)
    assert.ok(auth.authenticate(`Bearer ${tokenB}`))
    writeFileSync(replacement, '{broken')
    renameSync(replacement, fixture.policy); now += 1001
    assert.equal(auth.authenticate(`Bearer ${tokenB}`), null)
    assert.match(auth.lastError, /invalid control policy/)
  } finally { rmSync(fixture.directory, { recursive: true, force: true }) }
})

test('credential registry rejects cross-domain reuse and recovers atomically after rotation', () => {
  const fixture = policyFixture()
  let now = 1000
  try {
    const manifest = join(fixture.directory, 'identities.json')
    for (const node of nodes) writeFileSync(join(fixture.directory, `${node}.secret`),
      `${node}-runtime-secret-01234567890123456789`)
    writeFileSync(manifest, JSON.stringify({ version: 1, nodes: [...nodes].map(id =>
      ({ id, secret_file: `${id}.secret` })) }))
    const auth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes, now: () => now })
    const identities = new RuntimeIdentityStore({ manifestFile: manifest, nodeIds: nodes,
      now: () => now })
    const registry = new CredentialRegistry({
      observationToken: 'observation-token-012345678901234567890',
      controlAuthorizer: auth, runtimeIdentities: identities, now: () => now,
    })
    assert.equal(registry.reload(true), true)
    assert.ok(auth.authenticate(`Bearer ${tokenA}`, false))

    writeFileSync(join(fixture.directory, 'source.token'),
      'generator-runtime-secret-01234567890123456789')
    now += 1001
    assert.equal(registry.reload(), false)
    assert.match(registry.lastError, /credential collision between control principal.*runtime identity/)
    assert.equal(auth.available, false)
    assert.equal(identities.available, false)

    writeFileSync(join(fixture.directory, 'source.token'), `${tokenC}\n`)
    now += 1001
    assert.equal(registry.reload(), true)
    assert.ok(auth.authenticate(`Bearer ${tokenC}`, false))
    assert.equal(identities.available, true)
  } finally { rmSync(fixture.directory, { recursive: true, force: true }) }
})

test('credential registry filters active and retired values through a bounded rotation overlap', () => {
  const fixture = policyFixture()
  let now = 1000
  try {
    const manifest = join(fixture.directory, 'identities.json')
    for (const node of nodes) writeFileSync(join(fixture.directory, `${node}.secret`),
      `${node}-runtime-secret-01234567890123456789`)
    writeFileSync(manifest, JSON.stringify({ version: 1, nodes: [...nodes].map(id =>
      ({ id, secret_file: `${id}.secret` })) }))
    const auth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes,
      now: () => now })
    const identities = new RuntimeIdentityStore({ manifestFile: manifest, nodeIds: nodes,
      now: () => now })
    const registry = new CredentialRegistry({ controlAuthorizer: auth,
      runtimeIdentities: identities, now: () => now, redactionOverlapMs: 2000 })
    assert.equal(registry.reload(true), true)
    assert.ok(registry.credentialValues().includes(tokenA))

    writeFileSync(join(fixture.directory, 'source.token'), `${tokenC}\n`, { mode: 0o600 })
    // A forced pre-fan-out refresh discovers a replacement even inside the
    // authorizer's ordinary one-second authentication polling interval.
    now += 1
    assert.equal(registry.reload(true), true)
    assert.ok(registry.credentialValues().includes(tokenC))
    assert.ok(registry.credentialValues().includes(tokenA))
    assert.ok(auth.authenticate(`Bearer ${tokenC}`, false))
    assert.equal(auth.authenticate(`Bearer ${tokenA}`, false), null)

    now += 2000
    assert.equal(registry.credentialValues().includes(tokenA), false)
    assert.ok(registry.credentialValues().includes(tokenC))
  } finally { rmSync(fixture.directory, { recursive: true, force: true }) }
})

test('previous credential manifest is restart-safe, redaction-only, bounded, and expires', () => {
  const fixture = policyFixture()
  let now = 10_000
  try {
    const previousToken = join(fixture.directory, 'previous.token')
    const previousRuntime = join(fixture.directory, 'previous-runtime.secret')
    const manifest = join(fixture.directory, 'previous.json')
    writeFileSync(previousToken, tokenA, { mode: 0o600 })
    writeFileSync(previousRuntime, 'previous-runtime-secret-012345678901234567', { mode: 0o600 })
    writeFileSync(manifest, JSON.stringify({ version: 1,
      expires_at: new Date(now + 2000).toISOString(), credentials: [
        { role: 'control_principal', id: 'source-operator', secret_file: 'previous.token' },
        { role: 'runtime_identity', id: 'generator', secret_file: 'previous-runtime.secret' },
      ] }), { mode: 0o600 })
    const previous = new PreviousCredentialStore({ manifestFile: manifest,
      now: () => now, redactionOverlapMs: 2000 })
    assert.equal(previous.lastError, null)
    assert.deepEqual(previous.credentialEntries().map(entry => entry.key),
      ['control_principal:source-operator', 'runtime_identity:generator'])

    // Previous values are not authenticators; only the authorizer's current
    // snapshot participates in authentication.
    writeFileSync(join(fixture.directory, 'source.token'), tokenC, { mode: 0o600 })
    const auth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes,
      now: () => now })
    const runtime = { reload: () => true, credentialEntries: () => [],
      invalidate(message) { this.lastError = message } }
    const registry = new CredentialRegistry({ controlAuthorizer: auth,
      runtimeIdentities: runtime, previousCredentials: previous, now: () => now,
      redactionOverlapMs: 2000 })
    assert.equal(registry.reload(true), true)
    assert.equal(auth.authenticate(`Bearer ${tokenA}`, false), null)
    assert.ok(auth.authenticate(`Bearer ${tokenC}`, false))
    assert.ok(registry.credentialValues().includes(tokenA))
    assert.ok(registry.credentialValues().includes('previous-runtime-secret-012345678901234567'))

    now += 2000
    assert.equal(previous.credentialEntries().length, 0)
    assert.equal(registry.credentialValues().includes(tokenA), false)
  } finally { rmSync(fixture.directory, { recursive: true, force: true }) }
})

test('previous credential manifest validation fails closed without exposing values', () => {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-previous-credentials-'))
  let now = 1000
  const secret = 'previous-sensitive-value-01234567890123456789'
  const secretPath = join(directory, 'previous.secret')
  const manifest = join(directory, 'previous.json')
  const writeManifest = value => writeFileSync(manifest, JSON.stringify(value), { mode: 0o600 })
  const validEntry = { role: 'control_principal', id: 'operator', secret_file: 'previous.secret' }
  try {
    writeFileSync(secretPath, secret, { mode: 0o600 })
    writeManifest({ version: 1, expires_at: new Date(now + 2001).toISOString(),
      credentials: [validEntry] })
    const tooLong = new PreviousCredentialStore({ manifestFile: manifest,
      now: () => now, redactionOverlapMs: 2000 })
    assert.match(tooLong.lastError, /exceeds the 2000 ms overlap/)
    assert.equal(tooLong.lastError.includes(secret), false)

    writeManifest({ version: 1, expires_at: new Date(now + 1000).toISOString(), credentials: [] })
    assert.equal(tooLong.reload(true), false)
    assert.match(tooLong.lastError, /must contain at least one/)

    writeManifest({ version: 1, expires_at: '1970-01-01T00:00:02Z',
      credentials: [validEntry] })
    assert.equal(tooLong.reload(true), false)
    assert.match(tooLong.lastError, /canonical UTC timestamp/)

    writeManifest({ version: 1, expires_at: new Date(now + 1000).toISOString(), credentials: [
      { ...validEntry, role: 'observation', id: 'not-the-reserved-id' },
    ] })
    assert.equal(tooLong.reload(true), false)
    assert.match(tooLong.lastError, /requires its reserved id/)

    writeManifest({ version: 1, expires_at: new Date(now + 1000).toISOString(),
      credentials: [validEntry] })
    chmodSync(manifest, 0o622)
    assert.equal(tooLong.reload(true), false)
    assert.match(tooLong.lastError, /manifest must not be group- or world-writable/)
    chmodSync(manifest, 0o600)
    chmodSync(secretPath, 0o622)
    assert.equal(tooLong.reload(true), false)
    assert.match(tooLong.lastError, /must not be group- or world-writable/)
    assert.equal(tooLong.lastError.includes(secret), false)
    chmodSync(secretPath, 0o600)

    writeManifest({ version: 1, expires_at: new Date(now + 1000).toISOString(), credentials: [
      validEntry, { ...validEntry, id: 'second' },
    ] })
    assert.equal(tooLong.reload(true), false)
    assert.match(tooLong.lastError, /share a value/)

    writeManifest({ version: 1, expires_at: new Date(now - 1).toISOString(), credentials: [] })
    assert.equal(tooLong.reload(true), true)
    assert.deepEqual(tooLong.credentialEntries(), [])
  } finally { rmSync(directory, { recursive: true, force: true }) }
})

test('previous credential cross-role collision and combined capacity fail closed', () => {
  const fixture = policyFixture()
  let now = 1000
  try {
    const manifest = join(fixture.directory, 'previous.json')
    writeFileSync(join(fixture.directory, 'previous.secret'), tokenA, { mode: 0o600 })
    writeFileSync(manifest, JSON.stringify({ version: 1,
      expires_at: new Date(now + 2000).toISOString(), credentials: [
        { role: 'runtime_identity', id: 'generator', secret_file: 'previous.secret' },
      ] }), { mode: 0o600 })
    const previous = new PreviousCredentialStore({ manifestFile: manifest,
      now: () => now, redactionOverlapMs: 2000 })
    const auth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes,
      now: () => now })
    const runtime = { reload: () => true, credentialEntries: () => [],
      invalidate(message) { this.lastError = message } }
    const collision = new CredentialRegistry({ controlAuthorizer: auth,
      runtimeIdentities: runtime, previousCredentials: previous, now: () => now,
      redactionOverlapMs: 2000 })
    assert.equal(collision.reload(true), false)
    assert.match(collision.lastError, /credential collision between control principal.*previous runtime identity/)
    assert.equal(collision.lastError.includes(tokenA), false)

    // The same logical role may be staged before the current file is switched.
    writeFileSync(manifest, JSON.stringify({ version: 1,
      expires_at: new Date(now + 2000).toISOString(), credentials: [
        { role: 'control_principal', id: 'source-operator', secret_file: 'previous.secret' },
      ] }), { mode: 0o600 })
    const sameActiveAuth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes,
      now: () => now })
    const sameActivePrevious = new PreviousCredentialStore({ manifestFile: manifest,
      now: () => now, redactionOverlapMs: 2000 })
    const safelyStaged = new CredentialRegistry({ controlAuthorizer: sameActiveAuth,
      runtimeIdentities: runtime, previousCredentials: sameActivePrevious, now: () => now,
      redactionOverlapMs: 2000, maxRetiredCredentials: 0 })
    assert.equal(safelyStaged.reload(true), true)

    writeFileSync(join(fixture.directory, 'source.token'), tokenC, { mode: 0o600 })
    const stagedAuth = new ControlAuthorizer({ policyFile: fixture.policy, nodeIds: nodes,
      now: () => now })
    const stagedPrevious = new PreviousCredentialStore({ manifestFile: manifest,
      now: () => now, redactionOverlapMs: 2000 })
    const staged = new CredentialRegistry({ controlAuthorizer: stagedAuth,
      runtimeIdentities: runtime, previousCredentials: stagedPrevious, now: () => now,
      redactionOverlapMs: 2000, maxRetiredCredentials: 0 })
    assert.equal(staged.reload(true), false)
    assert.match(staged.lastError, /overlap capacity exceeded/)
  } finally { rmSync(fixture.directory, { recursive: true, force: true }) }
})

test('credential redaction overlap capacity fails closed', () => {
  let now = 1000
  let token = tokenA
  const control = { reload: () => true,
    credentialEntries: () => [{ role: 'control principal', value: token }],
    invalidate(message) { this.lastError = message } }
  const runtime = { reload: () => true, credentialEntries: () => [],
    invalidate(message) { this.lastError = message } }
  const registry = new CredentialRegistry({ controlAuthorizer: control,
    runtimeIdentities: runtime, now: () => now, redactionOverlapMs: 5000,
    maxRetiredCredentials: 1 })
  assert.equal(registry.reload(true), true)
  token = tokenB; now++
  assert.equal(registry.reload(true), true)
  token = tokenC; now++
  assert.equal(registry.reload(true), false)
  assert.match(registry.lastError, /redaction overlap capacity exceeded/)
  assert.equal(registry.lastError.includes(tokenA), false)
})

test('credential registry detects every supported cross-domain role pairing', () => {
  const credential = 'same-credential-value-012345678901234567890'
  const cases = [
    ['observation token', 'control principal'],
    ['observation token', 'runtime identity'],
    ['control principal', 'runtime identity'],
    ['observation token', 'shared telemetry HMAC'],
    ['control principal', 'shared telemetry HMAC'],
  ]
  for (const [left, right] of cases) {
    const values = new Map([[left, credential], [right, credential]])
    const component = prefix => ({ reload: () => true,
      credentialEntries: () => [...values].filter(([role]) => role.startsWith(prefix))
        .map(([role, value]) => ({ role, value })),
      invalidate(message) { this.lastError = message },
    })
    const registry = new CredentialRegistry({
      observationToken: values.get('observation token') || '',
      telemetrySecret: values.get('shared telemetry HMAC') || '',
      controlAuthorizer: component('control principal'),
      runtimeIdentities: component('runtime identity'),
    })
    assert.equal(registry.reload(true), false, `${left} and ${right}`)
    assert.match(registry.lastError, /credential collision between/)
    assert.equal(registry.lastError.includes(credential), false)
  }
})

test('commands have identity, target acknowledgement, timeout, and idempotency semantics', () => {
  let now = 10_000
  let nextId = 0
  const config = controlConfig({ command_timeout_ms: 100, command_retention_seconds: 60,
    max_commands: 4, max_audit_records: 10, idempotency_ttl_seconds: 60 })
  const plane = new ControlPlane(config, nodes,
    { now: () => now, uuid: () => `command-${++nextId}` })
  const delivered = []
  const issue = overrides => plane.issue({ action: 'pause', targetNodes: ['generator'],
    actor: 'source-operator', reason: 'maintenance', idempotencyKey: 'request-1', ...overrides },
  command => { delivered.push(command.id); return command.targetNodes.length })
  const first = issue({})
  assert.equal(first.command.status, 'pending')
  assert.deepEqual(delivered, ['command-1'])
  assert.equal(issue({}).replayed, true)
  assert.deepEqual(delivered, ['command-1'])
  assert.throws(() => issue({ action: 'resume' }), ControlConflictError)
  assert.equal(plane.acknowledge({ kind: 'control_ack', commandId: 'forged', nodeId: 'generator',
    action: 'pause', accepted: true }), false)
  assert.equal(plane.acknowledge({ kind: 'control_ack', commandId: 'command-1', nodeId: 'transform',
    action: 'pause', accepted: true }), false)
  assert.equal(plane.acknowledge({ kind: 'control_ack', commandId: 'command-1', nodeId: 'generator',
    action: 'pause', accepted: true, state: 'paused' }), true)
  assert.equal(plane.get('command-1').status, 'accepted')
  const second = issue({ idempotencyKey: 'request-2', action: 'resume' })
  now += 101
  assert.equal(plane.get(second.command.id).status, 'timed-out')
  assert.equal(plane.stats.accepted, 1)
  assert.equal(plane.stats.timedOut, 1)
})

test('bounded command and audit state reject overflow and redact credentials', () => {
  const config = controlConfig({ max_commands: 1, max_audit_records: 10 })
  const durable = []
  const plane = new ControlPlane(config, nodes,
    { uuid: () => 'one', auditSink: entry => durable.push(controlAuditHistoryRecord(entry, 'g')) })
  plane.deny({ actor: 'unknown', action: 'pause', targets: ['generator'], reason: 'bad credential' })
  plane.issue({ action: 'reset', targetNodes: ['collector'], actor: 'admin',
    reason: 'clear counters', idempotencyKey: null }, () => 1)
  assert.throws(() => plane.issue({ action: 'pause', targetNodes: ['generator'], actor: 'admin' },
    () => 1), /capacity exceeded/)
  assert.equal(JSON.stringify(plane.auditRecords()).includes(tokenA), false)
  assert.equal(durable.every(record => record.kind === 'control_audit'), true)
})

test('runtime rejection stores only allow-listed error codes in commands and audit', () => {
  const credentials = [tokenA, tokenB, tokenC,
    'observation-token-012345678901234567890',
    'legacy-hmac-secret-012345678901234567890',
    'generator-runtime-secret-01234567890123456789']
  const durable = []
  let sequence = 0
  const plane = new ControlPlane(controlConfig({ max_commands: 20, max_audit_records: 50 }), nodes,
    { uuid: () => `command-${++sequence}`,
      auditSink: entry => durable.push(controlAuditHistoryRecord(entry, 'g')) })
  for (const credential of credentials) {
    for (const error of [credential, `runtime failed with ${credential}`]) {
      const issued = plane.issue({ action: 'pause', targetNodes: ['generator'], actor: 'operator',
        idempotencyKey: null }, () => 1)
      assert.equal(plane.acknowledge({ kind: 'control_ack', commandId: issued.command.id,
        nodeId: 'generator', action: 'pause', accepted: false, error }), true)
      assert.equal(plane.get(issued.command.id).acknowledgements.generator.error,
        'runtime-rejected')
    }
  }
  const known = plane.issue({ action: 'resume', targetNodes: ['generator'], actor: 'operator',
    idempotencyKey: null }, () => 1)
  plane.acknowledge({ kind: 'control_ack', commandId: known.command.id, nodeId: 'generator',
    action: 'resume', accepted: false, error: 'busy' })
  assert.equal(plane.get(known.command.id).acknowledgements.generator.error, 'busy')
  const exposed = JSON.stringify({ commands: plane.list(), audit: plane.auditRecords(), durable })
  for (const credential of credentials) assert.equal(exposed.includes(credential), false)
})

test('legacy token remains an explicit all-node compatibility principal', () => {
  const auth = new ControlAuthorizer({ legacyToken: tokenA, nodeIds: nodes })
  const principal = auth.authenticate(`Bearer ${tokenA}`)
  assert.equal(principal.id, 'legacy-operator')
  assert.equal(auth.permits(principal, 'pause', ['generator', 'transform']), true)
  assert.equal(principal.permissions.has('audit:read'), true)
})
