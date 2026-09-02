export function mergeHistoryRecords(current, incoming, append) {
  if (!append) return [...incoming]
  const known = new Set(current.map(record => record.id))
  return [...current, ...incoming.filter(record => !known.has(record.id))]
}

export function isCurrentHistoryResponse(responseGeneration, currentGeneration) {
  return responseGeneration === currentGeneration
}
