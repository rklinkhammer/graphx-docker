export function decodeHex(hex) {
  if (typeof hex !== 'string' || hex.length > 131072 || !/^(?:[a-f0-9]{2})*$/.test(hex)) throw new Error('Invalid console bytes')
  return Uint8Array.from(hex.match(/../g) || [], byte => parseInt(byte, 16))
}
export function serialDelta(previous, current) {
  const bytes = decodeHex(current.hex)
  if (!Number.isSafeInteger(current.start) || !Number.isSafeInteger(current.end) ||
      current.start < 0 || current.end - current.start !== bytes.length) throw new Error('Invalid serial cursor')
  const reset = !previous || previous.generation !== current.generation || previous.end > current.end
  const gap = reset ? current.start > 0 : previous.end < current.start
  return { bytes: bytes.subarray(reset ? 0 : Math.max(0, previous.end - current.start)), reset, gap }
}
export function logText(value) {
  return new TextDecoder().decode(decodeHex(value.hex))
}
