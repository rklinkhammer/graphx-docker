export const RATE_WINDOW_SECONDS = 5
export const LATENCY_BOUNDS_US = [10, 50, 100, 500, 1000, 5000, 10000]

export function emptyEdge(connection = 'disconnected') {
  return {
    sent: 0, received: 0, sentWireBytes: 0, receivedWireBytes: 0,
    drops: 0, errors: 0, reconnects: 0, backpressureEvents: 0,
    backpressureUs: 0, rejected: 0, connection, lastSequence: 0, lastSeen: null,
    latencyCount: 0, latencySumUs: 0,
    latencyBuckets: Array(LATENCY_BOUNDS_US.length + 1).fill(0), rateBuckets: [],
  }
}

export function recordRate(edge, timestamp, wireBytes) {
  const second = Math.floor(timestamp / 1000)
  edge.rateBuckets = edge.rateBuckets.filter(bucket => bucket.second > second - RATE_WINDOW_SECONDS)
  let bucket = edge.rateBuckets.find(value => value.second === second)
  if (!bucket) {
    bucket = { second, messages: 0, wireBytes: 0 }
    edge.rateBuckets.push(bucket)
  }
  bucket.messages += 1
  bucket.wireBytes += wireBytes
}

export function currentRates(edge, timestamp = Date.now()) {
  const second = Math.floor(timestamp / 1000)
  const buckets = edge.rateBuckets.filter(bucket => bucket.second > second - RATE_WINDOW_SECONDS)
  if (!buckets.length) return { messageRate: 0, byteRate: 0 }
  const elapsed = Math.min(RATE_WINDOW_SECONDS,
    Math.max(1, second - Math.min(...buckets.map(bucket => bucket.second)) + 1))
  return {
    messageRate: Math.round(buckets.reduce((sum, bucket) => sum + bucket.messages, 0) / elapsed * 10) / 10,
    byteRate: Math.round(buckets.reduce((sum, bucket) => sum + bucket.wireBytes, 0) / elapsed),
  }
}

export function latencyPercentile(edge, percentile) {
  if (!edge.latencyCount) return null
  const target = Math.ceil(edge.latencyCount * percentile)
  let cumulative = 0
  for (let index = 0; index < edge.latencyBuckets.length; ++index) {
    cumulative += edge.latencyBuckets[index]
    if (cumulative >= target)
      return index < LATENCY_BOUNDS_US.length ? LATENCY_BOUNDS_US[index] : LATENCY_BOUNDS_US.at(-1)
  }
  return LATENCY_BOUNDS_US.at(-1)
}

export function edgeView(edge, timestamp = Date.now()) {
  return {
    sent: edge.sent, received: edge.received,
    sentWireBytes: edge.sentWireBytes, receivedWireBytes: edge.receivedWireBytes,
    ...currentRates(edge, timestamp), drops: edge.drops, errors: edge.errors,
    meanLatencyUs: edge.latencyCount ? Math.round(edge.latencySumUs / edge.latencyCount) : null,
    p95LatencyUs: latencyPercentile(edge, 0.95), latencyCount: edge.latencyCount,
    reconnects: edge.reconnects, backpressureEvents: edge.backpressureEvents,
    backpressureUs: edge.backpressureUs, rejected: edge.rejected,
    connection: edge.connection, lastSequence: edge.lastSequence, lastSeen: edge.lastSeen,
    metricSources: { counters: 'measured',
      latency: edge.latencyCount ? 'measured' : 'unavailable',
      throughput: 'derived-5s', drops: 'measured' },
  }
}

export function createMetricStore(topology) {
  const recent = []
  const captureReferences = []
  const nodes = Object.fromEntries(topology.nodes.map(node => [node.id, {
    status: 'starting', lastSeen: null, cpuPercent: null,
  }]))
  const edges = Object.fromEntries(topology.edges.map(edge => [edge.id, emptyEdge()]))

  function reset() {
    recent.length = 0
    captureReferences.length = 0
    for (const edge of Object.values(edges)) Object.assign(edge, emptyEdge(edge.connection))
  }

  function recordCapture(event) {
    captureReferences.unshift(event)
    if (captureReferences.length > 200) captureReferences.length = 200
  }

  function ingest(event, receivedAt) {
    if (nodes[event.nodeId]) {
      const cpuPercent = Number(event.cpuPercent)
      nodes[event.nodeId] = { ...nodes[event.nodeId], status: 'running', lastSeen: receivedAt,
        ...(Number.isFinite(cpuPercent) && cpuPercent >= 0 ? { cpuPercent } : {}) }
    }
    const edge = edges[event.edgeId]
    if (!edge) return
    edge.lastSeen = receivedAt
    edge.lastSequence = event.sequence || edge.lastSequence
    if (event.event === 'send') {
      edge.connection = 'connected'
      const wireBytes = Math.max(0, Number(event.wireBytes) || 0)
      edge.sent += 1; edge.sentWireBytes += wireBytes
      recordRate(edge, receivedAt, wireBytes)
    }
    if (event.kind === 'network_packet') {
      edge.connection = 'connected'
      const wireBytes = Math.max(0, Number(event.wireBytes) || 0)
      edge.sent += 1; edge.received += 1
      edge.sentWireBytes += wireBytes; edge.receivedWireBytes += wireBytes
      recordRate(edge, receivedAt, wireBytes)
      const definition = topology.edges.find(value => value.id === event.edgeId)
      for (const nodeId of [definition?.source, definition?.target])
        if (nodeId && nodes[nodeId]) nodes[nodeId] = { ...nodes[nodeId], status: 'running', lastSeen: receivedAt }
      recent.unshift(event)
      if (recent.length > 100) recent.length = 100
    } else if (event.event === 'receive') {
      edge.connection = 'connected'
      edge.received += 1
      edge.receivedWireBytes += Math.max(0, Number(event.wireBytes) || 0)
      const latencyUs = Math.max(0, Number(event.latencyUs) || 0)
      edge.latencyCount += 1; edge.latencySumUs += latencyUs
      let bucket = LATENCY_BOUNDS_US.findIndex(bound => latencyUs <= bound)
      if (bucket < 0) bucket = LATENCY_BOUNDS_US.length
      edge.latencyBuckets[bucket] += 1
      recent.unshift(event)
      if (recent.length > 100) recent.length = 100
    }
    if (event.event === 'error') { edge.errors += 1; edge.connection = 'error' }
    if (event.event === 'connection') edge.connection = event.message || 'unknown'
    if (event.event === 'reconnect') edge.reconnects += 1
    if (event.event === 'backpressure') {
      edge.backpressureEvents += 1; edge.backpressureUs += event.latencyUs || 0
      if (event.message === 'rejected') { edge.rejected += 1; edge.drops += 1 }
    }
    if (event.event === 'drop') edge.drops += 1
  }

  function recentWithCapture() {
    return recent.slice(0, 30).map(event => ({ ...event,
      captures: captureReferences.filter(reference =>
        (event.messageId ? reference.messageId === event.messageId :
          reference.traceId === event.traceId && reference.sequence === event.sequence) &&
        reference.edgeId === event.edgeId).slice(0, 4),
    }))
  }
  function resetNode(nodeId) {
    const fresh = createMetricStore(topology)
    Object.assign(nodes[nodeId], fresh.nodes[nodeId])
    for (const edge of topology.edges)
      if (edge.source === nodeId || edge.target === nodeId || edge.from === nodeId || edge.to === nodeId)
        Object.assign(edges[edge.id], fresh.edges[edge.id])
  }
  return { resetNode, nodes, edges, recent, captureReferences, ingest, recordCapture, reset, recentWithCapture }
}
