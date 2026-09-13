import { execFileSync } from 'node:child_process'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const directory = mkdtempSync(join(tmpdir(), 'graphx-telemetry-tests-'))
let sequence = 0

process.once('exit', () => rmSync(directory, { recursive: true, force: true }))

export function normalizedConfigEnvironment(source, overrides = '') {
  const graphx = process.env.NORMALIZED_CONFIG_CLI || resolve(repository, 'build/dev/graphx')
  const output = execFileSync(graphx, ['config', 'normalize', source], {
    encoding: 'utf8', env: { ...process.env, GRAPHX_OVERRIDES: '' },
  })
  const path = join(directory, `normalized-${sequence++}.json`)
  const config = JSON.parse(output)
  for (const entry of overrides.split(';').filter(Boolean)) {
    const [key, raw] = entry.split('=')
    const parts = key.split('.')
    let container = config
    for (const part of parts.slice(0, -1)) container = container[part] ||= {}
    let value
    try { value = JSON.parse(raw) } catch { value = raw }
    container[parts.at(-1)] = value
  }
  writeFileSync(path, JSON.stringify(config), { mode: 0o600 })
  return { GRAPHX_NORMALIZED_CONFIG: path, GRAPHX_CONFIG_DIRECTORY: dirname(source), GRAPHX_HISTORY_DATABASE_FILE: join(directory, `history-${sequence}.sqlite`) }
}
