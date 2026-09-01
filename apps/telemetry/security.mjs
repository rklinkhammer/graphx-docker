import { createHmac, randomBytes, timingSafeEqual } from 'node:crypto'
import { readFileSync } from 'node:fs'

const MAX_SECRET_BYTES = 4096
export const MAX_DATAGRAM_BYTES = 16384
export const MAX_CLOCK_SKEW_MS = 30000

export function readSecret(name, environment = process.env) {
  const inline = environment[name]
  const file = environment[`${name}_FILE`]
  if (inline && file) throw new Error(`${name} and ${name}_FILE are mutually exclusive`)
  let value = inline || ''
  if (file) value = readFileSync(file, { encoding: 'utf8', flag: 'r' }).replace(/\r?\n$/, '')
  if (Buffer.byteLength(value) > MAX_SECRET_BYTES) throw new Error(`${name} exceeds 4096 bytes`)
  if (value && Buffer.byteLength(value) < 32) throw new Error(`${name} must contain at least 32 bytes`)
  return value
}

export function tokenMatches(expected, authorization = '') {
  if (!expected || !authorization.startsWith('Bearer ')) return false
  const supplied = Buffer.from(authorization.slice(7))
  const wanted = Buffer.from(expected)
  return wanted.length === supplied.length && timingSafeEqual(wanted, supplied)
}

function signedText(payload, timestamp, nonce) {
  return `${timestamp}.${nonce}.${JSON.stringify(payload)}`
}

export function signEnvelope(payload, secret, timestamp = Date.now(), nonce = randomBytes(16).toString('hex')) {
  if (!secret) return payload
  const signature = createHmac('sha256', secret).update(signedText(payload, timestamp, nonce)).digest('hex')
  return { payload, auth: { timestamp, nonce, signature } }
}

export class ReplayCache {
  constructor(maxEntries = 4096, ttlMs = MAX_CLOCK_SKEW_MS * 2) {
    this.maxEntries = maxEntries
    this.ttlMs = ttlMs
    this.values = new Map()
  }
  accept(nonce, now = Date.now()) {
    for (const [value, seen] of this.values) if (now - seen > this.ttlMs) this.values.delete(value)
    if (this.values.has(nonce)) return false
    while (this.values.size >= this.maxEntries) this.values.delete(this.values.keys().next().value)
    this.values.set(nonce, now)
    return true
  }
}

export class RateLimiter {
  constructor(maxEntries = 2048) {
    if (!Number.isSafeInteger(maxEntries) || maxEntries < 1)
      throw new Error('rate limiter capacity must be a positive integer')
    this.maxEntries = maxEntries
    this.values = new Map()
  }
  allow(key, maximum, windowMs, now = Date.now()) {
    for (const [value, entry] of this.values)
      if (now >= entry.expiresAt) this.values.delete(value)
    const previous = this.values.get(key)
    const entry = !previous
      ? { count: 1, expiresAt: now + windowMs }
      : { ...previous, count: previous.count + 1 }
    if (!previous)
      while (this.values.size >= this.maxEntries)
        this.values.delete(this.values.keys().next().value)
    this.values.set(key, entry)
    return entry.count <= maximum
  }
  get size() { return this.values.size }
  has(key) { return this.values.has(key) }
}

export function parseRequestUrl(value) {
  try { return new URL(value, 'http://graphx.invalid') }
  catch { return null }
}

export function verifyEnvelope(value, secret, replayCache, now = Date.now()) {
  if (!secret) return value?.payload && value?.auth ? null : value
  const { payload, auth } = value || {}
  if (!payload || !auth || !Number.isSafeInteger(auth.timestamp) ||
      typeof auth.nonce !== 'string' || !/^[a-f0-9]{32}$/.test(auth.nonce) ||
      typeof auth.signature !== 'string' || !/^[a-f0-9]{64}$/.test(auth.signature) ||
      Math.abs(now - auth.timestamp) > MAX_CLOCK_SKEW_MS) return null
  const expected = createHmac('sha256', secret)
    .update(signedText(payload, auth.timestamp, auth.nonce)).digest()
  const supplied = Buffer.from(auth.signature, 'hex')
  if (expected.length !== supplied.length || !timingSafeEqual(expected, supplied)) return null
  if (replayCache && !replayCache.accept(auth.nonce, now)) return null
  return payload
}

const events = new Set(['send', 'receive', 'error', 'connection', 'reconnect', 'backpressure',
  'drop', 'processing', 'heartbeat', 'frame'])
const identities = value => value == null || (typeof value === 'string' && value.length <= 128)
const boundedNumber = (value, maximum = Number.MAX_SAFE_INTEGER) =>
  value == null || (Number.isFinite(value) && value >= 0 && value <= maximum)

export function validateTelemetryEvent(event, nodeIds, edgeIds) {
  if (!event || typeof event !== 'object' || Array.isArray(event) ||
      !['trace', 'capture', 'control_ack'].includes(event.kind) ||
      typeof event.nodeId !== 'string' || !nodeIds.has(event.nodeId)) return false
  if (event.kind === 'control_ack')
    return ['pause', 'resume'].includes(event.action) && typeof event.accepted === 'boolean'
  if (!events.has(event.event) || !boundedNumber(event.timestamp) ||
      !boundedNumber(event.sequence) || !boundedNumber(event.wireBytes, 64 * 1024 * 1024) ||
      !boundedNumber(event.latencyUs, 24 * 60 * 60 * 1e6) ||
      !boundedNumber(event.cpuPercent, 1000) || !identities(event.messageId) ||
      !identities(event.parentMessageId) || !identities(event.traceId) ||
      (event.edgeId && !edgeIds.has(event.edgeId))) return false
  if (event.kind === 'capture')
    return event.event === 'frame' && typeof event.captureFile === 'string' &&
      /^[A-Za-z][A-Za-z0-9_-]{0,63}\.pcapng$/.test(event.captureFile) &&
      ['sent', 'received'].includes(event.direction) && boundedNumber(event.capturePacket) &&
      boundedNumber(event.captureOffset)
  return true
}

export function isLoopback(host) {
  return host === '127.0.0.1' || host === '::1' || host === 'localhost'
}

export function originAllowed(origin, allowedOrigins) {
  if (!origin) return true
  return allowedOrigins.has(origin)
}

export function webSocketBearer(protocols = [], observationToken = '') {
  for (const protocol of protocols) {
    if (!protocol.startsWith('graphx-auth.')) continue
    try { return Buffer.from(protocol.slice(12), 'base64url').toString('utf8') }
    catch { return '' }
  }
  return observationToken ? '' : null
}
