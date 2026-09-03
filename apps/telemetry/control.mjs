import { createHash, randomUUID } from 'node:crypto'
import { readFileSync, statSync } from 'node:fs'
import { dirname, isAbsolute, resolve } from 'node:path'
import { sanitizeControlAcknowledgement, tokenMatches } from './security.mjs'

const CONTROL_ACTIONS = new Set(['pause', 'resume', 'reset'])
const CONTROL_PERMISSIONS = new Set([...CONTROL_ACTIONS, 'commands:read:any', 'audit:read'])
const CONTROL_CONFIG_KEYS = new Set([
  'command_timeout_ms', 'command_retention_seconds', 'max_commands', 'max_audit_records',
  'idempotency_ttl_seconds', 'max_request_bytes',
])
const MAX_POLICY_BYTES = 64 * 1024
const MAX_PRINCIPALS = 64
export const CREDENTIAL_REDACTION_OVERLAP_MS = 60 * 1000
const MAX_RETIRED_CREDENTIALS = 4096
const PREVIOUS_CREDENTIAL_ROLES = new Set([
  'observation', 'shared_telemetry', 'control_principal', 'runtime_identity',
])

function integer(value, fallback, minimum, maximum, name) {
  if (value == null) return fallback
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < minimum || value > maximum)
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}`)
  return value
}

export function controlConfig(config = {}) {
  if (!config || typeof config !== 'object' || Array.isArray(config))
    throw new Error('control configuration must be an object')
  for (const key of Object.keys(config))
    if (!CONTROL_CONFIG_KEYS.has(key)) throw new Error(`unknown control configuration property '${key}'`)
  return {
    commandTimeoutMs: integer(config.command_timeout_ms, 2000, 100, 30000,
      'control command_timeout_ms'),
    commandRetentionSeconds: integer(config.command_retention_seconds, 3600, 60, 86400,
      'control command_retention_seconds'),
    maxCommands: integer(config.max_commands, 1024, 1, 10000, 'control max_commands'),
    maxAuditRecords: integer(config.max_audit_records, 4096, 10, 100000,
      'control max_audit_records'),
    idempotencyTtlSeconds: integer(config.idempotency_ttl_seconds, 3600, 60, 86400,
      'control idempotency_ttl_seconds'),
    maxRequestBytes: integer(config.max_request_bytes, 4096, 256, 16384,
      'control max_request_bytes'),
  }
}

function identifier(value, name) {
  if (typeof value !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(value))
    throw new Error(`${name} is invalid`)
  return value
}

function readBounded(path, maximum, name) {
  const size = statSync(path).size
  if (size > maximum) throw new Error(`${name} exceeds ${maximum} bytes`)
  return readFileSync(path, 'utf8')
}

function readProtectedBounded(path, maximum, name) {
  const metadata = statSync(path)
  if (!metadata.isFile()) throw new Error(`${name} must be a regular file`)
  if ((metadata.mode & 0o022) !== 0) throw new Error(`${name} must not be group- or world-writable`)
  if (metadata.size > maximum) throw new Error(`${name} exceeds ${maximum} bytes`)
  return readFileSync(path, 'utf8')
}

function loadPolicy(path, nodeIds) {
  const base = dirname(path)
  let value
  try { value = JSON.parse(readBounded(path, MAX_POLICY_BYTES, 'control policy')) }
  catch (error) { throw new Error(`invalid control policy: ${error.message}`) }
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.version !== 1 ||
      !Array.isArray(value.principals) || value.principals.length < 1 ||
      value.principals.length > MAX_PRINCIPALS ||
      Object.keys(value).some(key => !['version', 'principals'].includes(key)))
    throw new Error('control policy must be version 1 with 1-64 principals')
  const ids = new Set()
  const tokenDigests = new Set()
  return value.principals.map((entry, index) => {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry) ||
        Object.keys(entry).some(key => !['id', 'token_file', 'permissions', 'nodes'].includes(key)))
      throw new Error(`control policy principal ${index} has unknown or invalid properties`)
    const id = identifier(entry.id, `control policy principal ${index} id`)
    if (ids.has(id)) throw new Error(`duplicate control principal '${id}'`)
    ids.add(id)
    if (typeof entry.token_file !== 'string' || !entry.token_file || entry.token_file.length > 1024 ||
        entry.token_file.includes('\0')) throw new Error(`control principal '${id}' token_file is invalid`)
    const tokenPath = isAbsolute(entry.token_file) ? entry.token_file : resolve(base, entry.token_file)
    const token = readBounded(tokenPath, 4096, `control principal '${id}' token`).replace(/\r?\n$/, '')
    if (Buffer.byteLength(token) < 32) throw new Error(`control principal '${id}' token is too short`)
    const digest = Buffer.from(token).toString('base64')
    if (tokenDigests.has(digest)) throw new Error('control principals must not share a token')
    tokenDigests.add(digest)
    if (!Array.isArray(entry.permissions) || !entry.permissions.length ||
        entry.permissions.some(action => !CONTROL_PERMISSIONS.has(action)))
      throw new Error(`control principal '${id}' permissions are invalid`)
    if (!Array.isArray(entry.nodes) || !entry.nodes.length || entry.nodes.length > 1024 ||
        entry.nodes.some(node => node !== '*' && node !== 'collector' && !nodeIds.has(node)))
      throw new Error(`control principal '${id}' nodes are invalid`)
    return { id, token, permissions: new Set(entry.permissions), nodes: new Set(entry.nodes) }
  })
}

export class ControlAuthorizer {
  constructor({ policyFile = '', legacyToken = '', nodeIds = new Set(), now = Date.now } = {}) {
    this.policyFile = policyFile
    this.legacyToken = legacyToken
    this.nodeIds = nodeIds
    this.now = now
    this.lastCheck = 0
    this.lastError = null
    this.principals = []
    this.reload(true)
  }

  get available() { return this.principals.length > 0 }

  reload(force = false) {
    if (!this.policyFile) {
      this.principals = this.legacyToken ? [{ id: 'legacy-operator', token: this.legacyToken,
        permissions: new Set(CONTROL_PERMISSIONS), nodes: new Set(['*']) }] : []
      this.lastError = null
      return true
    }
    const now = this.now()
    if (!force && now - this.lastCheck < 1000) return this.lastError == null
    this.lastCheck = now
    try {
      statSync(this.policyFile)
      // Token files are independent secret projections and may rotate without
      // changing the policy inode or mtime, so re-read the bounded policy and
      // its token files on every throttled check.
      this.principals = loadPolicy(this.policyFile, this.nodeIds)
      this.lastError = null
      return true
    } catch (error) {
      this.principals = []
      this.lastError = String(error.message).slice(0, 256)
      return false
    }
  }

  authenticate(authorization = '', reload = true) {
    if (reload) this.reload()
    for (const principal of this.principals)
      if (tokenMatches(principal.token, authorization)) return principal
    return null
  }

  permits(principal, action, targets) {
    return Boolean(principal?.permissions.has(action) &&
      targets.every(node => principal.nodes.has('*') || principal.nodes.has(node)))
  }

  describe(principal) {
    if (!principal) return null
    return { id: principal.id, permissions: [...principal.permissions], nodes: [...principal.nodes] }
  }

  containsCredential(value) {
    return typeof value === 'string' && this.principals.some(principal => value.includes(principal.token))
  }

  credentialEntries() {
    return this.principals.map(principal => ({ role: `control principal '${principal.id}'`,
      key: `control_principal:${principal.id}`, value: principal.token }))
  }

  invalidate(message) {
    this.principals = []
    this.lastError = String(message).slice(0, 256)
  }
}

function loadRuntimeIdentities(path, nodeIds) {
  const base = dirname(path)
  let value
  try { value = JSON.parse(readBounded(path, MAX_POLICY_BYTES, 'runtime identity manifest')) }
  catch (error) { throw new Error(`invalid runtime identity manifest: ${error.message}`) }
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.version !== 1 ||
      !Array.isArray(value.nodes) || value.nodes.length !== nodeIds.size ||
      Object.keys(value).some(key => !['version', 'nodes'].includes(key)))
    throw new Error('runtime identity manifest must contain exactly every configured node')
  const identities = new Map()
  const secrets = new Set()
  for (const [index, entry] of value.nodes.entries()) {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry) ||
        Object.keys(entry).some(key => !['id', 'secret_file'].includes(key)))
      throw new Error(`runtime identity ${index} has unknown or invalid properties`)
    const id = identifier(entry.id, `runtime identity ${index} id`)
    if (!nodeIds.has(id) || identities.has(id)) throw new Error(`runtime identity '${id}' is unknown or duplicated`)
    if (typeof entry.secret_file !== 'string' || !entry.secret_file || entry.secret_file.length > 1024 ||
        entry.secret_file.includes('\0')) throw new Error(`runtime identity '${id}' secret_file is invalid`)
    const secretPath = isAbsolute(entry.secret_file) ? entry.secret_file : resolve(base, entry.secret_file)
    const secret = readBounded(secretPath, 4096, `runtime identity '${id}' secret`).replace(/\r?\n$/, '')
    if (Buffer.byteLength(secret) < 32) throw new Error(`runtime identity '${id}' secret is too short`)
    const digest = Buffer.from(secret).toString('base64')
    if (secrets.has(digest)) throw new Error('runtime identities must not share a secret')
    secrets.add(digest)
    identities.set(id, secret)
  }
  return identities
}

export class RuntimeIdentityStore {
  constructor({ manifestFile = '', nodeIds = new Set(), now = Date.now } = {}) {
    this.manifestFile = manifestFile
    this.nodeIds = nodeIds
    this.now = now
    this.lastCheck = 0
    this.lastError = null
    this.identities = new Map()
    this.reload(true)
  }

  get available() { return this.identities.size === this.nodeIds.size && this.nodeIds.size > 0 }

  reload(force = false) {
    if (!this.manifestFile) { this.identities.clear(); this.lastError = null; return true }
    const now = this.now()
    if (!force && now - this.lastCheck < 1000) return this.lastError == null
    this.lastCheck = now
    try {
      this.identities = loadRuntimeIdentities(this.manifestFile, this.nodeIds)
      this.lastError = null
      return true
    } catch (error) {
      this.identities.clear()
      this.lastError = String(error.message).slice(0, 256)
      return false
    }
  }

  secretFor(nodeId, reload = true) {
    if (reload) this.reload()
    return this.identities.get(nodeId) || ''
  }
  containsCredential(value) {
    return typeof value === 'string' && [...this.identities.values()].some(secret => value.includes(secret))
  }

  credentialEntries() {
    return [...this.identities].map(([id, value]) => ({ role: `runtime identity '${id}'`,
      key: `runtime_identity:${id}`, value }))
  }

  invalidate(message) {
    this.identities.clear()
    this.lastError = String(message).slice(0, 256)
  }
}

function previousCredentialRole(entry) {
  if (!PREVIOUS_CREDENTIAL_ROLES.has(entry.role))
    throw new Error(`previous credential '${entry.id}' role is invalid`)
  const descriptions = {
    observation: 'observation token',
    shared_telemetry: 'shared telemetry HMAC',
    control_principal: `control principal '${entry.id}'`,
    runtime_identity: `runtime identity '${entry.id}'`,
  }
  return descriptions[entry.role]
}

function loadPreviousCredentials(path, now, overlapMs, maximumCredentials) {
  const base = dirname(path)
  let value
  try {
    value = JSON.parse(readProtectedBounded(path, MAX_POLICY_BYTES,
      'previous credential manifest'))
  } catch (error) { throw new Error(`invalid previous credential manifest: ${error.message}`) }
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.version !== 1 ||
      typeof value.expires_at !== 'string' || !Array.isArray(value.credentials) ||
      value.credentials.length > maximumCredentials ||
      Object.keys(value).some(key => !['version', 'expires_at', 'credentials'].includes(key)))
    throw new Error(`previous credential manifest must be version 1 with 0-${maximumCredentials} credentials`)
  const expiresAt = Date.parse(value.expires_at)
  if (!Number.isFinite(expiresAt) || new Date(expiresAt).toISOString() !== value.expires_at)
    throw new Error('previous credential manifest expires_at must be a canonical UTC timestamp')
  if (expiresAt > now + overlapMs)
    throw new Error(`previous credential manifest expires_at exceeds the ${overlapMs} ms overlap`)
  const ids = new Set()
  const descriptors = value.credentials.map((entry, index) => {
    if (!entry || typeof entry !== 'object' || Array.isArray(entry) ||
        Object.keys(entry).some(key => !['role', 'id', 'secret_file'].includes(key)))
      throw new Error(`previous credential ${index} has unknown or invalid properties`)
    const id = identifier(entry.id, `previous credential ${index} id`)
    const role = previousCredentialRole({ ...entry, id })
    if ((entry.role === 'observation' && id !== 'observation') ||
        (entry.role === 'shared_telemetry' && id !== 'shared'))
      throw new Error(`previous credential role '${entry.role}' requires its reserved id`)
    const key = `${entry.role}:${id}`
    if (ids.has(key)) throw new Error(`duplicate previous credential role '${key}'`)
    ids.add(key)
    if (typeof entry.secret_file !== 'string' || !entry.secret_file ||
        entry.secret_file.length > 1024 || entry.secret_file.includes('\0'))
      throw new Error(`previous credential '${id}' secret_file is invalid`)
    return { id, role, key, secretFile: entry.secret_file }
  })
  if (expiresAt <= now) return { expiresAt, entries: [] }
  if (!descriptors.length)
    throw new Error('unexpired previous credential manifest must contain at least one credential')
  const fingerprints = new Map()
  const entries = descriptors.map(entry => {
    const secretPath = isAbsolute(entry.secretFile) ? entry.secretFile : resolve(base, entry.secretFile)
    const secret = readProtectedBounded(secretPath, 4096,
      `previous credential '${entry.id}' secret`).replace(/\r?\n$/, '')
    if (Buffer.byteLength(secret) < 32)
      throw new Error(`previous credential '${entry.id}' secret is too short`)
    const fingerprint = credentialFingerprint(secret)
    const previous = fingerprints.get(fingerprint)
    if (previous) throw new Error(`previous credentials '${previous}' and '${entry.id}' share a value`)
    fingerprints.set(fingerprint, entry.id)
    return { role: `previous ${entry.role}`, key: entry.key, value: secret, expiresAt }
  })
  return { expiresAt, entries }
}

// A deployment-owned, redaction-only transition manifest preserves recently
// superseded values across collector restarts. It never participates in
// authentication and is deliberately time- and size-bounded.
export class PreviousCredentialStore {
  constructor({ manifestFile = '', now = Date.now,
    redactionOverlapMs = CREDENTIAL_REDACTION_OVERLAP_MS,
    maxCredentials = MAX_RETIRED_CREDENTIALS } = {}) {
    this.manifestFile = manifestFile
    this.now = now
    this.redactionOverlapMs = redactionOverlapMs
    this.maxCredentials = maxCredentials
    this.lastCheck = 0
    this.lastError = null
    this.expiresAt = 0
    this.entries = []
    this.reload(true)
  }

  reload(force = false) {
    if (!this.manifestFile) {
      this.entries = []
      this.expiresAt = 0
      this.lastError = null
      return true
    }
    const now = this.now()
    if (!force && now - this.lastCheck < 1000) return this.lastError == null
    this.lastCheck = now
    try {
      const loaded = loadPreviousCredentials(this.manifestFile, now,
        this.redactionOverlapMs, this.maxCredentials)
      this.entries = loaded.entries
      this.expiresAt = loaded.expiresAt
      this.lastError = null
      return true
    } catch (error) {
      this.entries = []
      this.expiresAt = 0
      this.lastError = String(error.message).slice(0, 256)
      return false
    }
  }

  credentialEntries() {
    if (this.now() >= this.expiresAt) return []
    return this.entries
  }

  invalidate(message) {
    this.entries = []
    this.expiresAt = 0
    this.lastError = String(message).slice(0, 256)
  }
}

function credentialFingerprint(value) {
  return createHash('sha256').update(value).digest('hex')
}

// Authentication components parse their files independently, but only this
// registry publishes them as one usable trust configuration. Because Node runs
// this synchronously, a colliding candidate is invalidated before any request
// or datagram can observe it.
export class CredentialRegistry {
  constructor({ observationToken = '', telemetrySecret = '', controlAuthorizer,
    runtimeIdentities, previousCredentials = null, now = Date.now,
    redactionOverlapMs = CREDENTIAL_REDACTION_OVERLAP_MS,
    maxRetiredCredentials = MAX_RETIRED_CREDENTIALS } = {}) {
    this.observationToken = observationToken
    this.telemetrySecret = telemetrySecret
    this.controlAuthorizer = controlAuthorizer
    this.runtimeIdentities = runtimeIdentities
    this.previousCredentials = previousCredentials || {
      reload: () => true, credentialEntries: () => [], invalidate: () => {},
    }
    this.now = now
    this.lastError = null
    this.nextReloadAt = 0
    this.redactionOverlapMs = redactionOverlapMs
    this.maxRetiredCredentials = maxRetiredCredentials
    this.activeCredentials = []
    this.retiredCredentials = new Map()
  }

  reload(force = false) {
    const now = this.now()
    this.#pruneRetired(now)
    if (!force && this.lastError && now < this.nextReloadAt) return false
    const controlValid = this.controlAuthorizer.reload(force)
    const runtimeValid = this.runtimeIdentities.reload(force)
    const previousValid = this.previousCredentials.reload(force)
    if (!controlValid || !runtimeValid || !previousValid) {
      const invalidRoles = [!controlValid ? 'control policy' : null,
        !runtimeValid ? 'runtime identity manifest' : null,
        !previousValid ? 'previous credential manifest' : null].filter(Boolean).join(' and ')
      return this.#invalidate(`invalid ${invalidRoles}`)
    }
    const activeEntries = [
      ...(this.observationToken ? [{ role: 'observation token', key: 'observation:observation',
        value: this.observationToken }] : []),
      ...(this.telemetrySecret ? [{ role: 'shared telemetry HMAC', key: 'shared_telemetry:shared',
        value: this.telemetrySecret }] : []),
      ...this.controlAuthorizer.credentialEntries(),
      ...this.runtimeIdentities.credentialEntries(),
    ]
    const previousEntries = this.previousCredentials.credentialEntries()
    const fingerprints = new Map()
    for (const entry of activeEntries) {
      const fingerprint = credentialFingerprint(entry.value)
      const previous = fingerprints.get(fingerprint)
      if (previous)
        return this.#invalidate(`credential collision between ${previous.role} and ${entry.role}`)
      fingerprints.set(fingerprint, entry)
    }
    for (const entry of previousEntries) {
      const fingerprint = credentialFingerprint(entry.value)
      const existing = fingerprints.get(fingerprint)
      // Staging a transition manifest before switching the current file is safe
      // only for the same logical role. Cross-role reuse remains fail-closed.
      if (existing && existing.key !== entry.key)
        return this.#invalidate(`credential collision between ${existing.role} and ${entry.role}`)
      if (!existing) fingerprints.set(fingerprint, entry)
    }
    const nextCredentials = activeEntries.map(entry => entry.value)
    const nextFingerprints = new Set(nextCredentials.map(credentialFingerprint))
    for (const value of this.activeCredentials) {
      const fingerprint = credentialFingerprint(value)
      if (!nextFingerprints.has(fingerprint))
        this.retiredCredentials.set(fingerprint,
          { value, expiresAt: now + this.redactionOverlapMs })
    }
    const activeFingerprints = new Set(activeEntries.map(entry => credentialFingerprint(entry.value)))
    const redactionFingerprints = new Set([
      ...[...this.retiredCredentials.keys()].filter(fingerprint => !activeFingerprints.has(fingerprint)),
      ...previousEntries.map(entry => credentialFingerprint(entry.value))
        .filter(fingerprint => !activeFingerprints.has(fingerprint)),
    ])
    if (redactionFingerprints.size > this.maxRetiredCredentials)
      return this.#invalidate('credential redaction overlap capacity exceeded')
    this.activeCredentials = nextCredentials
    this.lastError = null
    this.nextReloadAt = 0
    return true
  }

  credentialValues() {
    this.#pruneRetired(this.now())
    return [...new Set([...this.activeCredentials,
      ...[...this.retiredCredentials.values()].map(entry => entry.value),
      ...this.previousCredentials.credentialEntries().map(entry => entry.value)])]
  }

  #pruneRetired(now) {
    for (const [fingerprint, entry] of this.retiredCredentials)
      if (now >= entry.expiresAt) this.retiredCredentials.delete(fingerprint)
  }

  #invalidate(message) {
    this.lastError = String(message).slice(0, 256)
    this.nextReloadAt = this.now() + 1000
    this.controlAuthorizer.invalidate(this.lastError)
    this.runtimeIdentities.invalidate(this.lastError)
    this.previousCredentials.invalidate(this.lastError)
    return false
  }
}

function publicCommand(command) {
  return { id: command.id, action: command.action, targetNodes: [...command.targetNodes],
    actor: command.actor, reason: command.reason, status: command.status,
    issuedAt: new Date(command.issuedAt).toISOString(), expiresAt: new Date(command.expiresAt).toISOString(),
    acknowledgements: Object.fromEntries(command.acknowledgements), delivered: command.delivered }
}

export class ControlConflictError extends Error {}

export class ControlPlane {
  constructor(config, nodeIds, { now = Date.now, uuid = randomUUID, auditSink = null } = {}) {
    this.config = config
    this.nodeIds = nodeIds
    this.now = now
    this.uuid = uuid
    this.auditSink = auditSink
    this.commands = new Map()
    this.idempotency = new Map()
    this.audit = []
    this.auditSequence = 0
    this.stats = { issued: 0, accepted: 0, rejected: 0, timedOut: 0,
      denied: 0, idempotentReplays: 0, auditDropped: 0 }
  }

  record({ actor = 'unauthenticated', action = null, targets = [], decision, commandId = null,
    reason = null }, timestamp = this.now()) {
    const entry = { sequence: ++this.auditSequence, timestamp: new Date(timestamp).toISOString(),
      actor, action, targetNodes: [...targets], decision, commandId, reason }
    if (this.audit.length >= this.config.maxAuditRecords) {
      this.audit.shift()
      this.stats.auditDropped++
    }
    this.audit.push(entry)
    try { this.auditSink?.(entry, timestamp) } catch { this.stats.auditDropped++ }
    return entry
  }

  deny(details) { this.stats.denied++; return this.record({ ...details, decision: 'denied' }) }

  issue({ action, targetNodes, actor, reason = null, idempotencyKey = null }, delivered) {
    this.#maintain()
    if (!CONTROL_ACTIONS.has(action)) throw new Error('unknown control action')
    const targets = [...new Set(targetNodes)]
    if (!targets.length || targets.length > 1024 ||
        targets.some(node => action === 'reset' ? node !== 'collector' : !this.nodeIds.has(node)))
      throw new Error('control targetNodes are invalid')
    if (reason != null && (typeof reason !== 'string' || reason.length < 1 || reason.length > 256))
      throw new Error('control reason must contain 1-256 characters')
    if (idempotencyKey != null && (typeof idempotencyKey !== 'string' ||
        !/^[A-Za-z0-9._:-]{1,128}$/.test(idempotencyKey)))
      throw new Error('Idempotency-Key is invalid')
    const fingerprint = JSON.stringify({ action, targets: [...targets].sort(), reason })
    const cacheKey = idempotencyKey ? `${actor}:${idempotencyKey}` : null
    const previous = cacheKey ? this.idempotency.get(cacheKey) : null
    if (previous) {
      if (previous.fingerprint !== fingerprint) throw new ControlConflictError('Idempotency-Key was reused for a different command')
      const previousCommand = this.get(previous.commandId)
      if (!previousCommand) {
        this.idempotency.delete(cacheKey)
      } else {
        this.stats.idempotentReplays++
        this.record({ actor, action, targets, decision: 'idempotent-replay', commandId: previous.commandId })
        return { command: previousCommand, replayed: true }
      }
    }
    if (this.commands.size >= this.config.maxCommands)
      throw new Error('control command capacity exceeded')
    const issuedAt = this.now()
    const command = { id: this.uuid(), action, targetNodes: targets, actor, reason,
      status: action === 'reset' ? 'accepted' : 'pending', issuedAt,
      expiresAt: issuedAt + this.config.commandTimeoutMs, acknowledgements: new Map(), delivered: 0 }
    command.delivered = delivered(command)
    if (action !== 'reset' && command.delivered < 1) throw new Error('no live authorized runtime nodes')
    this.commands.set(command.id, command)
    if (cacheKey) this.idempotency.set(cacheKey, { commandId: command.id, fingerprint,
      expiresAt: issuedAt + this.config.idempotencyTtlSeconds * 1000 })
    this.stats.issued++
    if (action === 'reset') this.stats.accepted++
    this.record({ actor, action, targets, decision: action === 'reset' ? 'accepted' : 'issued',
      commandId: command.id, reason })
    return { command: publicCommand(command), replayed: false }
  }

  acknowledge(event, timestamp = this.now()) {
    this.#maintain(timestamp)
    const safeEvent = sanitizeControlAcknowledgement(event)
    const command = this.commands.get(safeEvent.commandId)
    if (!command || command.status !== 'pending' || command.action !== safeEvent.action ||
        !command.targetNodes.includes(safeEvent.nodeId) ||
        command.acknowledgements.has(safeEvent.nodeId)) return false
    const acknowledgement = { accepted: safeEvent.accepted === true,
      receivedAt: new Date(timestamp).toISOString(),
      state: safeEvent.state === 'paused' || safeEvent.state === 'running' ? safeEvent.state : null,
      error: typeof safeEvent.error === 'string' ? safeEvent.error : null }
    command.acknowledgements.set(safeEvent.nodeId, acknowledgement)
    if (!acknowledgement.accepted) {
      command.status = 'rejected'; this.stats.rejected++
    } else if (command.acknowledgements.size === command.targetNodes.length) {
      command.status = 'accepted'; this.stats.accepted++
    }
    this.record({ actor: command.actor, action: command.action, targets: [safeEvent.nodeId],
      decision: acknowledgement.accepted ? 'acknowledged' : 'rejected', commandId: command.id,
      reason: acknowledgement.error }, timestamp)
    return true
  }

  get(id) { this.#maintain(); const value = this.commands.get(id); return value ? publicCommand(value) : null }
  list(limit = 100) {
    this.#maintain()
    return [...this.commands.values()].slice(-Math.min(limit, 100)).reverse().map(publicCommand)
  }
  pendingCount() {
    this.#maintain()
    return [...this.commands.values()].filter(command => command.status === 'pending').length
  }
  auditRecords(limit = 100) { return this.audit.slice(-Math.min(limit, 1000)).reverse() }

  #maintain(timestamp = this.now()) {
    for (const command of this.commands.values()) {
      if (command.status === 'pending' && timestamp >= command.expiresAt) {
        command.status = 'timed-out'; this.stats.timedOut++
        this.record({ actor: command.actor, action: command.action, targets: command.targetNodes,
          decision: 'timed-out', commandId: command.id }, timestamp)
      }
    }
    const commandCutoff = timestamp - this.config.commandRetentionSeconds * 1000
    for (const [id, command] of this.commands) if (command.issuedAt < commandCutoff) this.commands.delete(id)
    for (const [key, value] of this.idempotency) if (timestamp >= value.expiresAt) this.idempotency.delete(key)
  }
}

export function controlAuditHistoryRecord(entry, graphId, recordedAtMs = Date.now()) {
  const data = { actor: entry.actor, action: entry.action, targetNodes: entry.targetNodes,
    decision: entry.decision, commandId: entry.commandId, reason: entry.reason }
  return { graphId, recordedAtMs, eventAtMs: recordedAtMs, kind: 'control_audit',
    event: entry.decision, nodeId: entry.targetNodes.length === 1 ? entry.targetNodes[0] : null,
    edgeId: null, json: JSON.stringify(data) }
}
