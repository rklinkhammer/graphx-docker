// Bound managed console output without imposing file-size limits on SQLite/captures.
export function boundOutput(streams, configured) {
  if (configured === undefined) return
  if (!/^[0-9]+$/.test(configured)) throw new Error('E_LOG_BOUND: invalid output budget')
  let remaining = Number(configured)
  if (!Number.isSafeInteger(remaining) || remaining < 4096 || remaining > 16 * 1024 * 1024)
    throw new Error('E_LOG_BOUND: invalid output budget')
  for (const stream of streams) {
    const write = stream.write.bind(stream)
    stream.write = (chunk, encoding, callback) => {
      if (typeof encoding === 'function') { callback = encoding; encoding = undefined }
      const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk, encoding)
      const keep = Math.min(bytes.length, remaining)
      remaining -= keep
      if (keep) return write(bytes.subarray(0, keep), callback)
      if (callback) queueMicrotask(callback)
      return true
    }
  }
}
