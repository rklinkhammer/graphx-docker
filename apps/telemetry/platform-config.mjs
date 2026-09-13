import { isDeepStrictEqual } from 'node:util'
import { dirname, join, resolve } from 'node:path'
import { homedir } from 'node:os'
import { CredentialReader, readJson, validateManifest } from './credentials.mjs'
import { loadNormalizedConfig } from './normalized-config.mjs'
import { ControlAuthorizer, RuntimeIdentityStore } from './control.mjs'
import { tokenMatches } from './security.mjs'

export function loadPlatform(path, environment = process.env) {
  const baseDirectory = dirname(resolve(path))
  const platform = readJson(path)
  if (platform.normalization !== 'resolved.json' || platform.credential_manifest !== 'credentials.json')
    throw new Error('platform requires compiler-owned sibling artifacts')
  const config = loadNormalizedConfig(join(baseDirectory, 'resolved.json'))
  if (!isDeepStrictEqual(platform, config.platform)) throw new Error('platform differs from resolved configuration')
  const manifest = validateManifest(readJson(join(baseDirectory, 'credentials.json')))
  if (Object.keys(manifest.entries).sort().join() !== config.credential_references.slice().sort().join())
    throw new Error('credential manifest differs from resolved references')
  const state = resolve(environment.GX_STATE || join(process.platform === 'darwin'
    ? join(homedir(), 'Library/Application Support') : environment.XDG_STATE_HOME || join(homedir(), '.local/state'),
  'graphx', config.graph_id))
  const credentialRoot = environment.GX_CREDENTIALS
  if (!credentialRoot) throw new Error('GX_CREDENTIALS must select an explicitly staged reference directory')
  const expand = value => {
    const result = value.replaceAll('${GX_STATE}', state)
    if (result.includes('${')) throw new Error('unresolved platform path')
    return result
  }
  const history = { ...platform.history, database_file: expand(platform.history.database_file) }
  const capture = { ...platform.capture, directory: expand(platform.capture.directory) }
  return { config, platform, manifest, state, credentialRoot: resolve(credentialRoot), history, capture, baseDirectory }
}

const textSecret = bytes => {
  const token = bytes?.toString('utf8').replace(/\r?\n$/, '') || ''
  if (Buffer.byteLength(token) < 32 || Buffer.byteLength(token) > 4096) throw new Error('invalid credential length')
  return token
}
// Inject resolved reference readers into the existing authorization machinery.
export class PlatformCredentials {
  constructor(loaded, clock) {
    this.loaded = loaded
    this.reader = new CredentialReader(loaded.credentialRoot, loaded.manifest, 'platform', clock)
    this.runtimeNodes = loaded.config.nodes.filter(node => node.telemetry.credential)
    this.control = new ControlAuthorizer()
    this.runtime = new RuntimeIdentityStore()
    this.runtime.nodeIds = new Set(this.runtimeNodes.map(node => node.node_id))
    this.control.reload = () => !this.error
    this.runtime.reload = () => !this.error
    this.previous = { reload: () => !this.error, credentialEntries: () => this.previousEntries,
      invalidate: () => { this.previousEntries = [] } }
    this.control.authenticate = authorization => {
      for (const principal of this.control.principals)
        if (principal.tokens.some(token => tokenMatches(token, authorization))) return principal
      return null
    }
    this.control.permits = (principal, action, targets) => Boolean(principal &&
      targets.every(node => principal.grants.some(grant => grant.actions.includes(action) && grant.nodes.includes(node))))
    this.reload()
    if (this.error) throw new Error(this.error)
  }
  reload() {
    try {
      const previousEntries = [], runtimeTokens = new Map()
      const read = (ref, member, key) => {
        const bundle = this.reader.read(ref)
        const current = textSecret(bundle.members[member])
        const old = bundle.previous[member] ? textSecret(bundle.previous[member]) : null
        if (old) previousEntries.push({ role: key, key, value: old })
        return old ? [current, old] : [current]
      }
      const observation = read('observer', 'token', 'observation:observation')
      for (const node of this.runtimeNodes)
        runtimeTokens.set(node.node_id, read(node.telemetry.credential, 'hmac', `runtime_identity:${node.node_id}`))
      const principals = []
      if (this.loaded.platform.control.enabled) for (const grant of this.loaded.platform.control.grants) {
        // Keep each grant separate so merging node/action sets cannot broaden authorization.
        const tokens = read(grant.credential, 'token', `control_principal:${grant.credential}`)
        const existing = principals.find(principal => principal.id === grant.credential)
        if (existing) {
          existing.grants.push(grant)
          for (const action of grant.actions) existing.permissions.add(action)
          for (const node of grant.nodes) existing.nodes.add(node)
        } else principals.push({ id: grant.credential, token: tokens[0], tokens, grants: [grant],
          permissions: new Set(grant.actions), nodes: new Set(grant.nodes) })
      }
      this.observation = observation; this.runtimeTokens = runtimeTokens
      this.runtime.identities = new Map([...runtimeTokens].map(([id, tokens]) => [id, tokens[0]]))
      this.control.principals = principals; this.previousEntries = previousEntries; this.error = null
      return true
    } catch {
      this.error = 'staged credential configuration is invalid'
      this.observation = []; this.runtimeTokens = new Map(); this.previousEntries = []
      this.runtime.invalidate(this.error); this.control.invalidate(this.error)
      return false
    }
  }
  observe(authorization) { return this.observation.some(token => tokenMatches(token, authorization)) }
  otlp() {
    const ref = this.loaded.platform.otlp.credentials
    if (!ref) return {}
    const { members } = this.reader.read(ref)
    return { ca: members['ca.pem'], cert: members['cert.pem'], key: members['key.pem'],
      token: members.token ? textSecret(members.token) : '', servername: this.loaded.platform.otlp.server_name }
  }
}
