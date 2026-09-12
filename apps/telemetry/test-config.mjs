import { execFileSync } from 'node:child_process'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const directory = mkdtempSync(join(tmpdir(), 'graphx-telemetry-tests-'))
let sequence = 0

process.once('exit', () => rmSync(directory, { recursive: true, force: true }))

export function normalizedConfigEnvironment(source) {
  const graphx = process.env.NORMALIZED_CONFIG_CLI || resolve(repository, 'build/dev/graphx')
  const output = execFileSync(graphx, ['config', 'normalize', source], {
    encoding: 'utf8', env: { ...process.env, GRAPHX_OVERRIDES: '' },
  })
  const path = join(directory, `normalized-${sequence++}.json`)
  writeFileSync(path, output, { mode: 0o600 })
  return { GRAPHX_NORMALIZED_CONFIG: path, GRAPHX_CONFIG_DIRECTORY: dirname(source) }
}
