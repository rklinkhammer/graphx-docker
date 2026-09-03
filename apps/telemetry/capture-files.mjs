import { closeSync, constants as fsConstants, fstatSync, openSync, opendirSync, readSync } from 'node:fs'
import { join } from 'node:path'

const MAX_INITIAL_BLOCK_BYTES = 256 * 1024
const CAPTURE_NAME = /^[A-Za-z][A-Za-z0-9_-]{0,63}\.pcapng$/

function positionedRead(descriptor, length, position) {
  const bytes = Buffer.alloc(length)
  let total = 0
  while (total < length) {
    const count = readSync(descriptor, bytes, total, length - total, position + total)
    if (count === 0) return null
    total += count
  }
  return bytes
}

function initialBlock(descriptor, position, expectedType, minimumLength, maximumBytes, fileSize) {
  const header = positionedRead(descriptor, 8, position)
  if (!header || header.readUInt32LE(0) !== expectedType) return null
  const length = header.readUInt32LE(4)
  if (length < minimumLength || length % 4 !== 0 || length > MAX_INITIAL_BLOCK_BYTES ||
      length > maximumBytes || position + length > fileSize) return null
  const bytes = positionedRead(descriptor, length, position)
  if (!bytes || bytes.readUInt32LE(length - 4) !== length) return null
  return bytes
}

export function pcapngLinkType(descriptor, fileSize, maximumBytes) {
  const section = initialBlock(descriptor, 0, 0x0a0d0d0a, 28, maximumBytes, fileSize)
  if (!section || section.readUInt32LE(8) !== 0x1a2b3c4d || section.readUInt16LE(12) !== 1 ||
      section.readUInt16LE(14) !== 0 || section.readBigUInt64LE(16) !== 0xffffffffffffffffn)
    return null
  const descriptorBlock = initialBlock(descriptor, section.length, 1, 20, maximumBytes, fileSize)
  return descriptorBlock ? descriptorBlock.readUInt16LE(8) : null
}

export function openValidatedCapture(path, maximumBytes) {
  let descriptor
  try {
    descriptor = openSync(path, fsConstants.O_RDONLY | fsConstants.O_NOFOLLOW | fsConstants.O_NONBLOCK)
    const details = fstatSync(descriptor)
    if (!details.isFile() || details.nlink !== 1 || details.size > maximumBytes)
      throw new Error('invalid capture')
    const linkType = pcapngLinkType(descriptor, details.size, maximumBytes)
    if (linkType !== 1 && linkType !== 147) throw new Error('invalid capture')
    return { descriptor, details, linkType }
  } catch (error) {
    if (descriptor != null) closeSync(descriptor)
    throw error
  }
}

export function listValidatedCaptures(directory, maximumBytes, { maxFiles, maxEntries }) {
  const candidates = []
  let scannedEntries = 0
  let truncated = false
  let handle
  try {
    handle = opendirSync(directory)
    while (scannedEntries < maxEntries) {
      const entry = handle.readSync()
      if (!entry) break
      scannedEntries += 1
      if (entry.isFile() && CAPTURE_NAME.test(entry.name)) candidates.push(entry.name)
    }
    if (scannedEntries === maxEntries && handle.readSync()) truncated = true
  } catch {
    return { captures: [], scannedEntries, truncated: false }
  } finally {
    try { handle?.closeSync() } catch {}
  }

  candidates.sort((left, right) => left < right ? -1 : left > right ? 1 : 0)
  const captures = []
  for (const name of candidates) {
    if (captures.length === maxFiles) { truncated = true; break }
    let capture
    try {
      capture = openValidatedCapture(join(directory, name), maximumBytes)
      captures.push({ name, details: capture.details, linkType: capture.linkType })
    } catch { /* Ignore invalid or concurrently removed directory entries. */ }
    finally { if (capture) closeSync(capture.descriptor) }
  }
  return { captures, scannedEntries, truncated }
}
