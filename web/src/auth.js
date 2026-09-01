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

