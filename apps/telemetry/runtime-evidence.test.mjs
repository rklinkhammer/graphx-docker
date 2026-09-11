import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import { createRuntimeEvidence, readBoundedJsonFile } from './runtime-evidence.mjs'

test('runtime evidence validates QEMU readiness and degrades stale guest evidence', t => {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-evidence-'))
  t.after(() => { try { process.getBuiltinModule('node:fs').rmSync(directory, { recursive: true }) } catch {} })
  const path = join(directory, 'qemu.json')
  writeFileSync(path, JSON.stringify({ state: 'ready', updatedAt: 1000, actualAccelerator: 'tcg',
    selectedAccelerator: 'tcg', requestedAccelerator: 'auto',
    evidenceSource: 'QMP query-status + query-kvm', qmpStatus: { running: true },
    kvm: { present: false, enabled: false }, vmState: 'running', guestState: 'ready',
    guestProtocols: { tcp: true, udp: true }, guestReadinessAt: 1000 }))
  const evidence = createRuntimeEvidence({ qemuEvidenceFile: path,
    topology: { edges: [] }, now: () => 12_000 }).qemuEvidence()
  assert.equal(evidence.state, 'degraded')
  assert.equal(evidence.reason, 'guest application readiness evidence is stale')
})

test('bounded JSON reader rejects oversized and malformed files', t => {
  const directory = mkdtempSync(join(tmpdir(), 'graphx-json-'))
  t.after(() => { try { process.getBuiltinModule('node:fs').rmSync(directory, { recursive: true }) } catch {} })
  const path = join(directory, 'value.json')
  writeFileSync(path, '{invalid')
  assert.equal(readBoundedJsonFile(path), null)
  writeFileSync(path, JSON.stringify({ value: 'x'.repeat(100) }))
  assert.equal(readBoundedJsonFile(path, 16), null)
})
