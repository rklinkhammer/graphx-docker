import assert from 'node:assert/strict'
import { spawn, spawnSync } from 'node:child_process'
import { once } from 'node:events'
import { createServer } from 'node:net'
import { closeSync, readSync } from 'node:fs'
import { link, mkdtemp, mkdir, readFile, rename, rm, symlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import test from 'node:test'
import { listValidatedCaptures, openValidatedCapture, capturePath } from './capture-files.mjs'
import { normalizedConfigEnvironment } from './test-config.mjs'

const here = dirname(fileURLToPath(import.meta.url))
const repository = join(here, '../..')

async function availablePort() {
  const server = createServer()
  await new Promise((resolve, reject) => server.listen(0, '127.0.0.1', resolve).once('error', reject))
  const port = server.address().port
  await new Promise(resolve => server.close(resolve))
  return port
}

function block(type, body) {
  const length = body.length + 12
  const result = Buffer.alloc(length)
  result.writeUInt32LE(type, 0); result.writeUInt32LE(length, 4)
  body.copy(result, 8); result.writeUInt32LE(length, length - 4)
  return result
}

function option(code, payload) {
  const result = Buffer.alloc(4 + Math.ceil(payload.length / 4) * 4)
  result.writeUInt16LE(code, 0); result.writeUInt16LE(payload.length, 2)
  payload.copy(result, 4)
  return result
}

function minimalPcapng(linkType = 147, sectionCommentBytes = 0) {
  const fixedSection = Buffer.alloc(16)
  fixedSection.writeUInt32LE(0x1a2b3c4d, 0); fixedSection.writeUInt16LE(1, 4)
  fixedSection.writeBigUInt64LE(0xffffffffffffffffn, 8)
  const section = sectionCommentBytes
    ? Buffer.concat([fixedSection, option(1, Buffer.alloc(sectionCommentBytes, 0x61)), Buffer.alloc(4)])
    : fixedSection
  const descriptor = Buffer.alloc(8)
  descriptor.writeUInt16LE(linkType, 0); descriptor.writeUInt32LE(16777220, 4)
  return Buffer.concat([block(0x0a0d0d0a, section), block(1, descriptor)])
}

test('capture downloads are bounded and reject symlink traversal', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-http-'))
  const captures = join(directory, 'captures')
  await mkdir(captures)
  await writeFile(join(captures, 'valid.pcapng'), minimalPcapng())
  const secret = join(directory, 'secret.txt')
  await writeFile(secret, 'must-not-be-downloaded')
  await symlink(secret, join(captures, 'leak.pcapng'))
  const port = await availablePort()
  const udpPort = await availablePort()
  const environment = { ...process.env, PORT: String(port), GRAPHX_TELEMETRY_PORT: String(udpPort),
    ...normalizedConfigEnvironment(join(repository, 'examples/sample-pipeline/graphx.yml'),
      'platform.capture.max_file_bytes=65536;platform.capture.max_packets=2'),
    GRAPHX_CAPTURE_ENABLED: 'true', GRAPHX_CAPTURE_DIR: captures }
  for (const key of Object.keys(environment))
    if (key.startsWith('GRAPHX_CONTROL_') || key.startsWith('GRAPHX_RUNTIME_') ||
        key === 'GRAPHX_PREVIOUS_CREDENTIALS_FILE') delete environment[key]
  const child = spawn(process.execPath, ['server.mjs'], { cwd: here, env: environment,
    stdio: ['ignore', 'pipe', 'pipe'] })
  t.after(async () => {
    if (child.exitCode == null) {
      child.kill('SIGTERM')
      await once(child, 'exit')
    }
    await rm(directory, { recursive: true, force: true })
  })
  const base = `http://127.0.0.1:${port}`
  let ready = false
  for (let attempt = 0; attempt < 50; attempt++) {
    try {
      if ((await fetch(`${base}/api/ready`)).ok) { ready = true; break }
    } catch {}
    await new Promise(resolve => setTimeout(resolve, 20))
  }
  assert.equal(ready, true, 'telemetry server did not become ready')
  const listing = await (await fetch(`${base}/api/captures`)).json()
  assert.deepEqual(listing.limits, { snaplen: 65535, maxFileBytes: 65536, maxPackets: 2,
    catalogMaxFiles: 128, catalogMaxEntries: 512 })
  assert.equal(listing.files.some(file => file.name === 'valid.pcapng'), true)
  assert.equal(listing.files.some(file => file.name === 'leak.pcapng'), false)
  const valid = await fetch(`${base}/captures/valid.pcapng`)
  assert.equal(valid.status, 200)
  assert.deepEqual(Buffer.from(await valid.arrayBuffer()), minimalPcapng())
  const leaked = await fetch(`${base}/captures/leak.pcapng`)
  assert.equal(leaked.status, 404)
  assert.equal((await leaked.text()).includes('must-not-be-downloaded'), false)
})

test('capture catalog bounds filesystem work and snapshot payloads without gating downloads', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-catalog-http-'))
  const captures = join(directory, 'captures')
  await mkdir(captures)
  const names = []
  for (let index = 0; index < 20; ++index) {
    const name = `file${String(index).padStart(2, '0')}.pcapng`
    names.push(name)
    await writeFile(join(captures, name), minimalPcapng())
  }
  const port = await availablePort()
  const udpPort = await availablePort()
  const environment = { ...process.env, PORT: String(port), GRAPHX_TELEMETRY_PORT: String(udpPort),
    ...normalizedConfigEnvironment(join(repository, 'examples/sample-pipeline/graphx.yml'),
      'platform.capture.max_file_bytes=65536'), GRAPHX_CAPTURE_ENABLED: 'true',
    GRAPHX_CAPTURE_DIR: captures,
    GRAPHX_CAPTURE_CATALOG_MAX_FILES: '4', GRAPHX_CAPTURE_CATALOG_MAX_ENTRIES: '10' }
  for (const key of Object.keys(environment))
    if (key.startsWith('GRAPHX_CONTROL_') || key.startsWith('GRAPHX_RUNTIME_') ||
        key === 'GRAPHX_PREVIOUS_CREDENTIALS_FILE') delete environment[key]
  const child = spawn(process.execPath, ['server.mjs'], { cwd: here, env: environment,
    stdio: ['ignore', 'pipe', 'pipe'] })
  t.after(async () => {
    if (child.exitCode == null) {
      child.kill('SIGTERM')
      await once(child, 'exit')
    }
    await rm(directory, { recursive: true, force: true })
  })
  const base = `http://127.0.0.1:${port}`
  let ready = false
  for (let attempt = 0; attempt < 50; attempt++) {
    try {
      if ((await fetch(`${base}/api/ready`)).ok) { ready = true; break }
    } catch {}
    await new Promise(resolve => setTimeout(resolve, 20))
  }
  assert.equal(ready, true, 'telemetry server did not become ready')

  const listing = await (await fetch(`${base}/api/captures`)).json()
  assert.equal(listing.files.length, 4)
  assert.equal(listing.catalogScannedEntries, 10)
  assert.equal(listing.catalogTruncated, true)
  assert.equal(listing.limits.catalogMaxFiles, 4)
  assert.equal(listing.limits.catalogMaxEntries, 10)
  const topology = await (await fetch(`${base}/api/topology`)).json()
  assert.equal(topology.capture.files.length, 4)
  assert.equal(topology.capture.catalogTruncated, true)

  const omitted = names.find(name => !listing.files.some(file => file.name === name))
  assert.ok(omitted)
  const download = await fetch(`${base}/captures/${omitted}`)
  assert.equal(download.status, 200)
  assert.deepEqual(Buffer.from(await download.arrayBuffer()), minimalPcapng())
})

test('invalid capture catalog deployment limits fail before listeners start', async () => {
  assert.match(await rejectedCaptureStartup({ GRAPHX_CAPTURE_CATALOG_MAX_FILES: '0' }),
    /GRAPHX_CAPTURE_CATALOG_MAX_FILES must be an integer between 1 and 1024/)
  assert.match(await rejectedCaptureStartup({ GRAPHX_CAPTURE_CATALOG_MAX_FILES: '10',
    GRAPHX_CAPTURE_CATALOG_MAX_ENTRIES: '9' }),
  /GRAPHX_CAPTURE_CATALOG_MAX_ENTRIES must be an integer between 10 and 4096/)
})

test('capture validation and streaming retain the same descriptor across rename', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-descriptor-'))
  t.after(() => rm(directory, { recursive: true, force: true }))
  const path = join(directory, 'capture.pcapng')
  const moved = join(directory, 'validated.pcapng')
  const expected = minimalPcapng()
  await writeFile(path, expected)
  const capture = openValidatedCapture(path, 65536)
  try {
    await rename(path, moved)
    await writeFile(path, 'replacement that is not a capture')
    const actual = Buffer.alloc(capture.details.size)
    assert.equal(readSync(capture.descriptor, actual, 0, actual.length, 0), actual.length)
    assert.deepEqual(actual, expected)
    assert.equal(capture.linkType, 147)
  } finally { closeSync(capture.descriptor) }
})

test('capture validation rejects hardlinks and FIFOs without blocking', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-file-type-'))
  t.after(() => rm(directory, { recursive: true, force: true }))
  const original = join(directory, 'original.pcapng')
  const hardlink = join(directory, 'hardlink.pcapng')
  await writeFile(original, minimalPcapng())
  await link(original, hardlink)
  assert.throws(() => openValidatedCapture(hardlink, 65536), /invalid capture/)

  const fifo = join(directory, 'capture.pcapng')
  const created = spawnSync('mkfifo', [fifo])
  assert.equal(created.status, 0, created.stderr.toString())
  const started = Date.now()
  assert.throws(() => openValidatedCapture(fifo, 65536), /invalid capture/)
  assert.ok(Date.now() - started < 1000, 'FIFO validation must not block')

  const unsupported = join(directory, 'unsupported-dlt.pcapng')
  await writeFile(unsupported, minimalPcapng(149))
  assert.throws(() => openValidatedCapture(unsupported, 65536), /invalid capture/)
})

test('capture validation accepts bounded initial blocks larger than 512 bytes', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-large-header-'))
  t.after(() => rm(directory, { recursive: true, force: true }))
  const path = join(directory, 'large-header.pcapng')
  await writeFile(path, minimalPcapng(147, 700))
  const capture = openValidatedCapture(path, 65536)
  try { assert.equal(capture.linkType, 147) } finally { closeSync(capture.descriptor) }
})

test('capture validation rejects incomplete and inconsistent packet blocks', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-completeness-'))
  t.after(() => rm(directory, { recursive: true, force: true }))
  const fields = Buffer.alloc(20)
  fields.writeUInt32LE(32, 12)
  fields.writeUInt32LE(1, 16)
  const inconsistent = join(directory, 'inconsistent.pcapng')
  await writeFile(inconsistent, Buffer.concat([minimalPcapng(1), block(6,
    Buffer.concat([fields, Buffer.alloc(32)]))]))
  assert.throws(() => openValidatedCapture(inconsistent, 65536), /incomplete capture/)

  const complete = Buffer.concat([minimalPcapng(1), block(6,
    Buffer.concat([Buffer.alloc(20), Buffer.alloc(4)]))])
  const partial = join(directory, 'partial.pcapng')
  await writeFile(partial, complete.subarray(0, complete.length - 2))
  assert.throws(() => openValidatedCapture(partial, 65536), /incomplete capture/)
})

test('capture catalog bounds directory work and returns sorted truncation metadata', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-catalog-'))
  t.after(() => rm(directory, { recursive: true, force: true }))
  for (let index = 19; index >= 0; --index)
    await writeFile(join(directory, `file${String(index).padStart(2, '0')}.pcapng`), minimalPcapng())
  const catalog = listValidatedCaptures(directory, 65536, { maxFiles: 4, maxEntries: 10 })
  assert.equal(catalog.scannedEntries, 10)
  assert.equal(catalog.truncated, true)
  assert.equal(catalog.captures.length, 4)
  const names = catalog.captures.map(capture => capture.name)
  assert.deepEqual(names, [...names].sort((left, right) => left < right ? -1 : left > right ? 1 : 0))
})

async function rejectedCaptureStartup(overrides, configPath = join(repository, 'examples/sample-pipeline/graphx.yml')) {
  const environment = { ...process.env, PORT: String(await availablePort()),
    GRAPHX_TELEMETRY_PORT: String(await availablePort()), ...normalizedConfigEnvironment(configPath) }
  delete environment.GRAPHX_CAPTURE_ENABLED
  Object.assign(environment, overrides)
  const child = spawn(process.execPath, ['server.mjs'], { cwd: here, env: environment,
    stdio: ['ignore', 'pipe', 'pipe'] })
  let errors = ''
  child.stderr.on('data', chunk => { errors += chunk })
  const timer = setTimeout(() => child.kill('SIGKILL'), 2000)
  const [code] = await once(child, 'exit')
  clearTimeout(timer)
  assert.notEqual(code, 0)
  return errors
}

function rejectedNormalization(configPath) {
  const graphx = process.env.NORMALIZED_CONFIG_CLI || join(repository, 'build/dev/graphx')
  const result = spawnSync(graphx, ['config', 'normalize', configPath], { encoding: 'utf8' })
  assert.notEqual(result.status, 0)
  return result.stderr
}

test('capture deployment toggle fails closed', async () => {
  assert.match(await rejectedCaptureStartup({ GRAPHX_CAPTURE_ENABLED: 'maybe' }), /must be one of/)
})

test('native normalization rejects unsafe capture types and explicit runtime paths', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-capture-config-'))
  t.after(() => rm(directory, {recursive: true, force: true}))
  for (const [key, value] of [['enabled', 'true'], ['max_files', '2'], ['max_packets', '10'], ['directory', '/tmp/escape']]) {
    const config = join(directory, `${key}.json`)
    await writeFile(config, JSON.stringify({version: 3, catalog: 'unused', graph: {id: 'test'}, nodes: {}, connections: {}, platform: {capture: {[key]: value}}}))
    assert.match(rejectedNormalization(config), /E_SCHEMA.*platform\.capture/)
  }
})

test('node capture directories expose bounded raw application records without path traversal', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'graphx-node-capture-'))
  t.after(() => rm(directory, { recursive: true, force: true }))
  await mkdir(join(directory, 'radio'))
  await writeFile(join(directory, 'radio/radio.pcapng'), minimalPcapng(148))
  const listed = listValidatedCaptures(directory, 65536, { maxFiles: 4, maxEntries: 4 })
  assert.equal(listed.captures.length, 1)
  assert.equal(listed.captures[0].name, 'radio.pcapng')
  assert.equal(listed.captures[0].linkType, 148)
  const opened = openValidatedCapture(capturePath(directory, 'radio.pcapng'), 65536)
  closeSync(opened.descriptor)
  assert.throws(() => capturePath(directory, '../radio.pcapng'), /invalid/)
  await symlink(join(directory, 'radio'), join(directory, 'alias'))
  assert.throws(() => capturePath(directory, 'alias.pcapng'), /invalid/)
  await writeFile(join(directory, 'radio.pcapng'), minimalPcapng(148))
  assert.throws(() => capturePath(directory, 'radio.pcapng'), /ambiguous/)
})
