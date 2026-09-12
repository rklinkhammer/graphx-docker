import { execFileSync } from 'node:child_process'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const source = resolve(process.argv[2] || join(repository, 'graphx.yaml'))
const graphx = process.env.GRAPHX_BIN || join(repository, 'build/dev/graphx')
const temporary = mkdtempSync(join(tmpdir(), 'graphx-telemetry-dev-'))
const normalized = join(temporary, 'normalized.json')

writeFileSync(normalized, execFileSync(graphx, ['config', 'normalize', source], {
  env: { ...process.env, GRAPHX_OVERRIDES: '' },
}))
process.env.GRAPHX_NORMALIZED_CONFIG = normalized
process.env.GRAPHX_CONFIG_DIRECTORY = dirname(source)
process.once('exit', () => rmSync(temporary, { recursive: true, force: true }))

await import('./server.mjs')
