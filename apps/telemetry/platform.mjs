#!/usr/bin/env node
import { mkdirSync, existsSync, unlinkSync, accessSync, constants } from 'node:fs'
import { dirname, resolve, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { DatabaseSync } from 'node:sqlite'
import { loadPlatform } from './platform-config.mjs'
import { readJson, stageCredentials, rotateCredential, safePath, validateInheritedLock } from './credentials.mjs'

if (process.argv.length === 3 && process.argv[2] === '--help') {
  console.log('graphx-platform --config FILE | stage --manifest FILE --destination DIR [--external DIR] | rotate --manifest FILE --destination DIR --credential REF --next REF [--grace SECONDS] | history-remove --config FILE --owner ID')
  process.exit(0)
}
const args = process.argv.slice(2)
const directory = dirname(fileURLToPath(import.meta.url))
const operation = args[0]?.startsWith('--') ? 'serve' : args.shift()
const options = new Map()
for (let index = 0; index < args.length; index += 2) {
  const key = args[index], value = args[index + 1]
  if (!['--config', '--manifest', '--destination', '--external', '--credential', '--next', '--grace', '--owner'].includes(key) ||
      !value || options.has(key)) throw new Error('invalid or duplicate platform argument')
  options.set(key, value)
}
const requireOption = key => { if (!options.has(key)) throw new Error(`${key} is required`); return options.get(key) }
function locked(path, arguments_) {
  const requested = process.env.GRAPHX_CLI || 'graphx'
  const candidates = requested.includes('/') ? [resolve(requested)] :
    (process.env.PATH || '').split(':').map(directory => join(directory, requested))
  const executable = candidates.find(path => { try { accessSync(path, constants.X_OK); return true } catch { return false } })
  if (!executable) throw new Error('graphx ownership lock executable is unavailable')
  process.execve(executable, [executable, 'platform-lock', '--lock', path, '--', process.execPath, ...arguments_], process.env)

}
if (operation === 'stage') {
  stageCredentials(readJson(requireOption('--manifest')), requireOption('--destination'),
    { externalRoot: options.get('--external') })
} else if (operation === 'rotate') {
  const root = safePath(requireOption('--destination'))
  const lock = join(root, '.lock')
  if (process.env.GRAPHX_PLATFORM_LOCK !== lock)
    locked(lock, [fileURLToPath(import.meta.url), 'rotate', ...args])
  else {
    validateInheritedLock(lock)
    rotateCredential(readJson(requireOption('--manifest')), root, requireOption('--credential'),
    requireOption('--next'), Number(options.get('--grace') ?? 60))
  }
} else if (operation === 'serve' || operation === 'history-remove') {
  const loaded = loadPlatform(requireOption('--config'))
  const database = resolve(loaded.history.database_file)
  const historyDirectory = dirname(database)
  if (operation === 'serve') mkdirSync(historyDirectory, { recursive: true, mode: 0o700 })
  safePath(historyDirectory)
  const lock = join(historyDirectory, '.lock')
  if (operation === 'serve') {
    if (!process.env.GX_OWNER || !/^[A-Za-z0-9_-]{32,128}$/.test(process.env.GX_OWNER))
      throw new Error('GX_OWNER must be a stable 32-128 character owner identity')
    locked(lock, [join(directory, 'server.mjs'), '--config', requireOption('--config')])
  } else if (process.env.GRAPHX_PLATFORM_LOCK !== lock) {
    locked(lock, [fileURLToPath(import.meta.url), 'history-remove', ...args])
  } else {
    validateInheritedLock(lock)
    const owner = requireOption('--owner')
    safePath(database)
    const db = new DatabaseSync(database, { readOnly: true })
    try {
      const metadata = db.prepare('SELECT value FROM history_metadata WHERE key=?')
      if (metadata.get('graph_id')?.value !== loaded.config.graph_id || metadata.get('owner')?.value !== owner)
        throw new Error('history ownership mismatch; nothing removed')
    } finally { db.close() }
    const files = ['', '-wal', '-shm'].map(suffix => `${database}${suffix}`).filter(existsSync)
    for (const path of files) safePath(path)
    for (const path of files) unlinkSync(path)
    console.log('Owned inactive history removed')
  }
} else throw new Error('usage: graphx-platform --config FILE | stage | rotate | history-remove')
