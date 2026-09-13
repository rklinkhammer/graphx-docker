// Reference provisioning only: graph semantics belong to the C++ resolver.
import { createHash, randomBytes } from 'node:crypto'
import { constants, openSync, closeSync, fstatSync, readSync, lstatSync,
  mkdirSync, writeFileSync, renameSync, chmodSync, rmSync, fsyncSync } from 'node:fs'
import { dirname, resolve, join, parse } from 'node:path'
import { execFileSync } from 'node:child_process'

const name = /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/
const hash = bytes => createHash('sha256').update(bytes).digest('hex')
export function safePath(path) {
  const absolute = resolve(path)
  let current = parse(absolute).root
  for (const part of absolute.slice(current.length).split('/').filter(Boolean)) {
    current = join(current, part)
    const metadata = lstatSync(current)
    if (metadata.isSymbolicLink()) throw new Error('symbolic links are forbidden in platform paths')
  }
  return absolute
}
export function readBounded(path, maximum = 65536, secret = false) {
  safePath(path)
  const fd = openSync(path, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK)
  try {
    const metadata = fstatSync(fd)
    if (!metadata.isFile() || metadata.nlink !== 1 || metadata.size > maximum ||
        (secret && (metadata.mode & 0o077))) throw new Error('invalid protected regular file')
    const bytes = Buffer.alloc(maximum + 1)
    let offset = 0
    while (offset < bytes.length) {
      const count = readSync(fd, bytes, offset, bytes.length - offset, null)
      if (!count) break
      offset += count
    }
    if (offset > maximum) throw new Error('platform file exceeds bound')
    return bytes.subarray(0, offset)
  } finally { closeSync(fd) }
}
export const readJson = path => JSON.parse(readBounded(path, 4 * 1024 * 1024))
export function validateManifest(manifest) {
  if (manifest?.version !== 1 || manifest.deny_inline_values !== true ||
      !manifest.entries || !manifest.allowed_consumers || Object.keys(manifest.entries).length > 4096 ||
      Object.keys(manifest).some(key => !['version', 'deny_inline_values', 'entries', 'allowed_consumers', 'bundle_contract'].includes(key)) ||
      Object.keys(manifest.allowed_consumers).sort().join() !== Object.keys(manifest.entries).sort().join())
    throw new Error('invalid credential reference manifest')
  for (const [ref, entry] of Object.entries(manifest.entries)) {
    if (Object.keys(entry).some(key => !['provider', 'identity', 'members', 'files', 'tls_roles'].includes(key)) ||
        !name.test(ref) || !['external', 'lab-generated', 'runtime-generated'].includes(entry.provider) ||
        !name.test(entry.identity) || !Array.isArray(entry.members) || !entry.members.length ||
        entry.members.length > 8 || new Set(entry.members).size !== entry.members.length ||
        entry.members.some(member => typeof member !== 'string' || !/^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$/.test(member) ||
          member === 'generation.json' || member.startsWith('previous.')) ||
        !Array.isArray(manifest.allowed_consumers[ref]) ||
        manifest.allowed_consumers[ref].some(consumer => !name.test(consumer)))
      throw new Error('invalid credential reference descriptor')
  }
  return manifest
}
function atomic(path, bytes) {
  const temporary = `${path}.${randomBytes(12).toString('hex')}.tmp`
  const fd = openSync(temporary, constants.O_CREAT | constants.O_EXCL | constants.O_WRONLY, 0o400)
  try { writeFileSync(fd, bytes); fsyncSync(fd) } finally { closeSync(fd) }
  renameSync(temporary, path)
}
function publicGeneration(entry, generation, members, previous = null) {
  return { version: 1, identity: entry.identity, generation,
    members: Object.fromEntries(Object.entries(members).map(([key, bytes]) => [key, hash(bytes)])),
    previous }
}
function provision(entry, ref, externalRoot, lab) {
  const members = {}
  if (entry.provider === 'external') {
    if (!externalRoot) throw new Error(`external credential reference '${ref}' requires an input directory`)
    for (const member of entry.members)
      members[member] = readBounded(join(externalRoot, ref, member), 65536, true)
  } else if (entry.provider === 'runtime-generated') {
    if (entry.members.some(member => !['hmac', 'token'].includes(member)))
      throw new Error('runtime-generated provider only produces tokens and HMAC keys')
    for (const member of entry.members) members[member] = Buffer.from(randomBytes(32).toString('hex'))
  } else {
    if (entry.members.slice().sort().join() !== 'ca.pem,cert.pem,key.pem')
      throw new Error('lab-generated provider requires ca.pem, cert.pem, key.pem')
    if (!Array.isArray(entry.tls_roles) || !entry.tls_roles.length || entry.tls_roles.some(role => !['serverAuth', 'clientAuth'].includes(role)))
      throw new Error('lab credential requires resolved TLS roles')
    const key = join(lab, `${ref}.key`), csr = join(lab, `${ref}.csr`), cert = join(lab, `${ref}.crt`)
    const extensions = join(lab, `${ref}.ext`)
    writeFileSync(extensions, `basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=${entry.tls_roles.join(',')}\nsubjectAltName=DNS:${entry.identity}\n`, { mode: 0o600 })
    openssl(['req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
      '-subj', `/CN=${entry.identity}`, '-keyout', key, '-out', csr])
    openssl(['x509', '-req', '-in', csr, '-CA', join(lab, 'ca.pem'), '-CAkey', join(lab, 'ca.key'),
      '-set_serial', `0x${randomBytes(16).toString('hex')}`, '-days', '1', '-extfile', extensions, '-out', cert])
    members['ca.pem'] = readBounded(join(lab, 'ca.pem'))
    members['cert.pem'] = readBounded(cert)
    members['key.pem'] = readBounded(key)
  }
  for (const [member, bytes] of Object.entries(members))
    if (!bytes.length || (['hmac', 'token'].includes(member) && (bytes.length < 32 || bytes.length > 4096)))
      throw new Error('credential member length is invalid')
  return members
}
function openssl(args) {
  execFileSync('openssl', args, { timeout: 10000, maxBuffer: 65536, stdio: ['ignore', 'pipe', 'pipe'] })
}
// Explicit staging creates an exclusive destination. It never executes graph actions.
export function stageCredentials(manifest, destination, { externalRoot } = {}) {
  validateManifest(manifest)
  safePath(dirname(resolve(destination)))
  mkdirSync(destination, { mode: 0o700 })
  const lab = join(destination, '.lab')
  try {
    if (Object.values(manifest.entries).some(entry => entry.provider === 'lab-generated')) {
      mkdirSync(lab, { mode: 0o700 })
      openssl(['req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes',
        '-days', '1', '-subj', '/CN=GraphX ephemeral lab CA', '-addext', 'basicConstraints=critical,CA:TRUE,pathlen:0',
        '-addext', 'keyUsage=critical,keyCertSign,cRLSign', '-keyout', join(lab, 'ca.key'), '-out', join(lab, 'ca.pem')])
    }
    for (const [ref, entry] of Object.entries(manifest.entries)) {
      const directory = join(destination, ref)
      mkdirSync(directory, { mode: 0o700 })
      const members = provision(entry, ref, externalRoot, lab)
      for (const [member, bytes] of Object.entries(members)) atomic(join(directory, member), bytes)
      atomic(join(directory, 'generation.json'), JSON.stringify(publicGeneration(entry, 1, members)))
      chmodSync(directory, 0o500)
    }
  } catch (error) {
    // Partial secrets remain protected for explicit owner cleanup; never publish success.
    throw new Error(`credential staging failed: ${error.message}`, { cause: error })
  } finally { rmSync(lab, { recursive: true, force: true }) }
}
export class CredentialReader {
  constructor(root, manifest, consumer, { now = () => performance.now(), wall = Date.now } = {}) {
    this.root = resolve(root); this.manifest = validateManifest(manifest); this.consumer = consumer
    this.now = now; this.wall = wall; this.generations = new Map()
  }
  read(ref) {
    const entry = this.manifest.entries[ref]
    if (!entry || !this.manifest.allowed_consumers[ref]?.includes(this.consumer))
      throw new Error('credential reference is unavailable to this consumer')
    const directory = join(this.root, ref)
    for (let attempt = 0; attempt < 3; attempt++) {
      const before = readBounded(join(directory, 'generation.json'), 8192, true)
      const metadata = JSON.parse(before)
      const known = this.generations.get(ref)
      if (metadata.version !== 1 || metadata.identity !== entry.identity ||
          !Number.isSafeInteger(metadata.generation) || metadata.generation < 1 ||
          (known && (metadata.generation < known.generation ||
            (metadata.generation === known.generation && known.hash !== hash(before)))) ||
          Object.keys(metadata.members || {}).sort().join() !== entry.members.slice().sort().join())
        throw new Error('credential generation is invalid or stale')
      const members = {}, previous = {}
      for (const member of entry.members) {
        members[member] = readBounded(join(directory, member), 65536, true)
        if (hash(members[member]) !== metadata.members[member]) throw new Error('credential generation is incomplete')
      }
      let deadline = known?.generation === metadata.generation ? known.deadline : 0
      if (metadata.previous) {
        const p = metadata.previous
        if (!Number.isSafeInteger(p.grace_ms) || p.grace_ms < 0 || p.grace_ms > 60000 ||
            !Number.isSafeInteger(p.expires_at)) throw new Error('credential overlap is invalid')
        if (!known || known.generation !== metadata.generation)
          deadline = this.now() + Math.max(0, Math.min(p.grace_ms, p.expires_at - this.wall()))
        if (this.now() < deadline) for (const member of entry.members) {
          previous[member] = readBounded(join(directory, `previous.${member}`), 65536, true)
          if (hash(previous[member]) !== p.members[member]) throw new Error('previous credential generation is incomplete')
        }
      }
      if (!before.equals(readBounded(join(directory, 'generation.json'), 8192, true))) continue
      this.generations.set(ref, { generation: metadata.generation, deadline, hash: hash(before) })
      return { members, previous, generation: metadata.generation }
    }
    throw new Error('credential generation changed while reading')
  }
}
// Caller holds the common exclusive ownership lock for this staging root.
export function rotateCredential(manifest, root, ref, nextRef, graceSeconds = 60) {
  validateManifest(manifest)
  if (!Number.isInteger(graceSeconds) || graceSeconds < 0 || graceSeconds > 60 || ref === nextRef)
    throw new Error('invalid rotation overlap or reference')
  const entry = manifest.entries[ref], next = manifest.entries[nextRef]
  if (!entry || !next || entry.provider !== next.provider || entry.identity !== next.identity ||
      entry.members.slice().sort().join() !== next.members.slice().sort().join())
    throw new Error('rotation must preserve provider, identity and member roles')
  // Provisioning owns the private staging root; an unused next reference has no runtime consumers.
  // This local read authority is never published in the manifest or mounted into a consumer.
  const provisioning = { ...manifest, allowed_consumers: { ...manifest.allowed_consumers,
    [ref]: ['provisioner'], [nextRef]: ['provisioner'] } }
  const reader = new CredentialReader(root, provisioning, 'provisioner')
  const current = reader.read(ref)
  const replacement = reader.read(nextRef)
  const directory = safePath(join(root, ref))
  chmodSync(directory, 0o700)
  try {
    // In-progress metadata deliberately fails closed; stable directory mounts survive replacement.
    atomic(join(directory, 'generation.json'), JSON.stringify({ version: 1, publishing: true }))
    for (const member of entry.members) {
      atomic(join(directory, `previous.${member}`), current.members[member])
      atomic(join(directory, member), replacement.members[member])
    }
    atomic(join(directory, 'generation.json'), JSON.stringify(publicGeneration(entry, current.generation + 1,
      replacement.members, { grace_ms: graceSeconds * 1000, expires_at: Date.now() + graceSeconds * 1000,
        members: Object.fromEntries(Object.entries(current.members).map(([key, value]) => [key, hash(value)])) })))
  } finally { chmodSync(directory, 0o500) }
}

export function validateInheritedLock(path) {
  if (process.env.GRAPHX_PLATFORM_LOCK !== path || !/^[0-9]+$/.test(process.env.GRAPHX_PLATFORM_LOCK_FD || ''))
    throw new Error('platform operation requires an inherited ownership lock')
  const opened = fstatSync(Number(process.env.GRAPHX_PLATFORM_LOCK_FD)), current = lstatSync(safePath(path))
  if (!opened.isFile() || opened.size !== 0 || opened.nlink !== 1 || (opened.mode & 0o777) !== 0o600 ||
      opened.dev !== current.dev || opened.ino !== current.ino)
    throw new Error('platform ownership lock identity changed')
}
