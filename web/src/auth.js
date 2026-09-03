export function bearerHeaders(token = '') {
  return token ? { Authorization: `Bearer ${token}` } : {}
}

export function webSocketProtocols(token = '') {
  if (!token) return ['graphx']
  const bytes = new TextEncoder().encode(token)
  let binary = ''
  for (const byte of bytes) binary += String.fromCharCode(byte)
  const encoded = btoa(binary).replaceAll('+', '-').replaceAll('/', '_').replaceAll('=', '')
  return ['graphx', `graphx-auth.${encoded}`]
}

export function persistObservationToken(storage, token = '') {
  if (token) storage.setItem('graphx-observation-token', token)
  else storage.removeItem('graphx-observation-token')
}

export function controlRequest(action, token = '') {
  return { url: `/api/control/${action}`, options: { method: 'POST', headers: bearerHeaders(token) } }
}

export function controlCommandRequest(action, token = '', targetNodes = null, reason = null,
  idempotencyKey = crypto.randomUUID()) {
  const body = { action }
  if (targetNodes != null) body.targetNodes = targetNodes
  if (reason != null) body.reason = reason
  return { url: '/api/control/commands', options: { method: 'POST', headers: {
    ...bearerHeaders(token), 'Content-Type': 'application/json', 'Idempotency-Key': idempotencyKey,
  }, body: JSON.stringify(body) } }
}

export function controlStatusRequest(commandId, token = '') {
  return { url: `/api/control/commands/${encodeURIComponent(commandId)}`,
    options: { method: 'GET', headers: bearerHeaders(token) } }
}
