import { closeSync, constants, fstatSync, openSync, readSync } from 'node:fs'

const qemuStates = new Set(['not-started', 'booting', 'ready', 'degraded', 'stopped', 'unavailable'])
const qemuAccelerators = new Set(['auto', 'kvm', 'tcg', 'hvf'])
const diagnosticEvidenceByState = new Map([
  ['allowed', 'receiver-confirmed'], ['policy-denied', 'nft-counter'],
  ['missing-route', 'route-absent'], ['route-applied', 'route-installed'],
  ['link-down', 'carrier-down'], ['attachment-missing', 'attachment-absent'],
  ['application-unavailable', 'receiver-unavailable'],
])
export const diagnosticLayerByState = new Map([
  ['allowed', 'application'], ['policy-denied', 'policy'], ['missing-route', 'route'],
  ['route-applied', 'route'], ['link-down', 'link'], ['attachment-missing', 'attachment'],
  ['application-unavailable', 'application'],
])

export function readBoundedJsonFile(path, maximum = 64 * 1024) {
  let descriptor
  try {
    descriptor = openSync(path, constants.O_RDONLY | (constants.O_NOFOLLOW || 0))
    const metadata = fstatSync(descriptor)
    if (!metadata.isFile() || metadata.size > maximum) return null
    const buffer = Buffer.alloc(Math.min(maximum + 1, metadata.size + 1))
    let offset = 0
    while (offset < buffer.length) {
      const count = readSync(descriptor, buffer, offset, buffer.length - offset, null)
      if (count === 0) break
      offset += count
    }
    if (offset > maximum) return null
    return JSON.parse(buffer.subarray(0, offset).toString('utf8'))
  } catch { return null } finally { if (descriptor != null) closeSync(descriptor) }
}

export function createRuntimeEvidence({ qemuEvidenceFile = '', networkDiagnosticFile = '', topology,
  now = () => Date.now() }) {
  function qemuEvidence() {
    if (!qemuEvidenceFile) return null
    const value = readBoundedJsonFile(qemuEvidenceFile)
    if (!value || typeof value !== 'object' || !qemuStates.has(value.state) ||
        !Number.isSafeInteger(value.updatedAt)) return null
    if (value.actualAccelerator != null && !qemuAccelerators.has(value.actualAccelerator)) return null
    if (value.selectedAccelerator != null && !qemuAccelerators.has(value.selectedAccelerator)) return null
    if (value.requestedAccelerator != null && !qemuAccelerators.has(value.requestedAccelerator)) return null
    if (value.evidenceSource != null && value.evidenceSource !== 'QMP query-status + query-kvm') return null
    if (value.state === 'ready' && (value.evidenceSource == null ||
        typeof value.qmpStatus?.running !== 'boolean' ||
        typeof value.kvm?.present !== 'boolean' || typeof value.kvm?.enabled !== 'boolean' ||
        value.vmState !== 'running' || value.guestState !== 'ready' ||
        value.guestProtocols?.tcp !== true || value.guestProtocols?.udp !== true ||
        !Number.isSafeInteger(value.guestReadinessAt))) return null
    if (value.state === 'ready' && now() - value.guestReadinessAt > 10_000)
      return { ...value, state: 'degraded', guestState: 'unavailable',
        reason: 'guest application readiness evidence is stale' }
    return value
  }

  function networkDiagnosticEvidence() {
    if (!networkDiagnosticFile) return null
    const value = readBoundedJsonFile(networkDiagnosticFile)
    if (value?.version !== 1 || !Number.isSafeInteger(value.updatedAt) ||
        typeof value.routeApplied !== 'boolean' || !value.flows ||
        typeof value.flows !== 'object' || Array.isArray(value.flows) ||
        Object.keys(value.flows).length > topology.edges.length) return null
    for (const [edgeId, flow] of Object.entries(value.flows)) {
      if (!topology.edges.some(edge => edge.id === edgeId) || !flow ||
          typeof flow !== 'object' || Array.isArray(flow) ||
          diagnosticEvidenceByState.get(flow.state) !== flow.evidence ||
          Object.keys(flow).some(key => !['state', 'evidence'].includes(key))) return null
    }
    if (value.routeApplied !== Object.values(value.flows)
      .some(flow => flow.state === 'route-applied')) return null
    return value
  }
  return { qemuEvidence, networkDiagnosticEvidence }
}
