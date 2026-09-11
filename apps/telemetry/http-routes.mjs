import { closeSync, createReadStream, existsSync } from 'node:fs'
import { extname, join, normalize, resolve } from 'node:path'
import { openValidatedCapture } from './capture-files.mjs'
import { parseHistoryQuery } from './history.mjs'
import { parseRequestUrl } from './security.mjs'

export function createHttpRequestHandler(context) {
  const { json, withinRateLimit, observationToken, authorized, snapshot, graphReadiness,
    nodes, edges, heartbeatTimeout, getSlo, historyStore, configuredHistory, nodeIds, edgeIds,
    packetHistoryUrl, securityHeaders, prometheus, controlPrincipal, controlPlane,
    requestOriginAllowed, readControlBody, issueControl, captureDirectory, captureConfig,
    root, types, serviceState, credentialRegistry, refreshCredentials, tlsEnabled } = context
const handleRequest = (request, response) => {
  if (!request.url || request.url.length > 2048) return json(response, 414, { error: 'request target too long' })
  const url = parseRequestUrl(request.url)
  if (!url) return json(response, 400, { error: 'invalid request target' })
  if (url.pathname === '/api/live') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, { status: 'live', service: 'graphx-telemetry' })
  }
  if (url.pathname === '/api/ready') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    refreshCredentials()
    const ready = serviceState.httpReady && serviceState.udpReady && !serviceState.shuttingDown &&
      credentialRegistry.lastError == null
    return json(response, ready ? 200 : 503, { status: ready ? 'ready' : 'not-ready', service: 'graphx-telemetry',
      listeners: { http: serviceState.httpReady, udp: serviceState.udpReady },
      credentialConfiguration: credentialRegistry.lastError == null ? 'valid' : 'invalid',
      shuttingDown: serviceState.shuttingDown })
  }
  if (url.pathname === '/api/health') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
    return json(response, 200, { status: 'live', service: 'graphx-telemetry', tls: tlsEnabled,
      serviceReady: serviceState.httpReady && serviceState.udpReady && !serviceState.shuttingDown &&
        credentialRegistry.lastError == null,
      graphReady: readiness.ready })
  }
  if (!withinRateLimit(request, 120)) return json(response, 429, { error: 'rate limit exceeded' }, { 'retry-after': '60' })
  const observed = ['/api/topology', '/api/captures', '/api/graph/ready', '/api/slo',
    '/api/history', '/api/history/status', '/api/packet-history', '/metrics'].includes(url.pathname) ||
    url.pathname.startsWith('/captures/')
  if (observed && observationToken && !authorized(request, observationToken))
    return json(response, 401, { error: 'invalid observation token' }, { 'www-authenticate': 'Bearer realm="graphx-observation"' })
  if (url.pathname === '/api/topology') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, snapshot())
  }
  if (url.pathname === '/api/graph/ready') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    const readiness = graphReadiness(nodes, edges, Date.now(), heartbeatTimeout)
    return json(response, readiness.ready ? 200 : 503,
      { status: readiness.ready ? 'ready' : 'not-ready', ...readiness })
  }
  if (url.pathname === '/api/slo') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, getSlo())
  }
  if (url.pathname === '/api/history/status') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, historyStore.stats)
  }
  if (url.pathname === '/api/history') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    let query
    try { query = parseHistoryQuery(url.searchParams, configuredHistory, nodeIds, edgeIds) }
    catch (error) { return json(response, 400, { error: error.message }) }
    historyStore.query({ ...query, excludeControlAudit: true })
      .then(result => json(response, 200, result))
      .catch(error => json(response, error.message.includes('capacity') ? 429 : 503,
        { error: String(error.message).slice(0, 256) },
        error.message.includes('capacity') ? { 'retry-after': '1' } : {}))
    return
  }
  if (url.pathname === '/api/packet-history') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    if (!packetHistoryUrl) return json(response, 503, { error: 'packet history is disabled' })
    const permitted = new Set(['limit', 'before', 'protocol'])
    if ([...url.searchParams.keys()].some(key => !permitted.has(key)))
      return json(response, 400, { error: 'unknown packet history query parameter' })
    const limit = url.searchParams.get('limit') || '100'
    const before = url.searchParams.get('before')
    const protocol = url.searchParams.get('protocol')
    if (!/^[1-9][0-9]{0,2}$/.test(limit) || Number(limit) > 500 ||
        (before != null && !/^[1-9][0-9]{0,18}$/.test(before)) ||
        (protocol != null && !['TCP', 'UDP'].includes(protocol)))
      return json(response, 400, { error: 'invalid packet history query' })
    const target = new URL('/history', packetHistoryUrl)
    target.searchParams.set('limit', limit)
    if (before) target.searchParams.set('before', before)
    if (protocol) target.searchParams.set('protocol', protocol)
    fetch(target, { signal: AbortSignal.timeout(2000) })
      .then(async upstream => {
        const body = await upstream.json()
        if (!response.headersSent) json(response, upstream.ok ? 200 : 503, body)
      })
      .catch(() => { if (!response.headersSent) json(response, 503, { error: 'packet history is unavailable' }) })
    return
  }
  if (url.pathname === '/api/captures') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    return json(response, 200, snapshot().capture)
  }
  if (url.pathname === '/metrics') {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    response.writeHead(200, { ...securityHeaders, 'cache-control': 'no-store',
      'content-type': 'text/plain; version=0.0.4; charset=utf-8' })
    return response.end(prometheus())
  }
  if (url.pathname === '/api/control/commands' && request.method === 'GET') {
    const principal = controlPrincipal(request)
    if (!principal)
      return json(response, 401, { error: 'invalid control credential' },
        { 'www-authenticate': 'Bearer realm="graphx-control"' })
    const canReadAll = principal.permissions.has('commands:read:any')
    const commands = controlPlane.list(100).filter(command => canReadAll || command.actor === principal.id)
    return json(response, 200, { commands })
  }
  const commandMatch = url.pathname.match(/^\/api\/control\/commands\/([0-9a-f-]{36})$/)
  if (commandMatch && request.method === 'GET') {
    const principal = controlPrincipal(request)
    if (!principal)
      return json(response, 401, { error: 'invalid control credential' },
        { 'www-authenticate': 'Bearer realm="graphx-control"' })
    const command = controlPlane.get(commandMatch[1])
    if (!command || (command.actor !== principal.id && !principal.permissions.has('commands:read:any')))
      return json(response, 404, { error: 'control command not found' })
    return json(response, 200, command)
  }
  if (url.pathname === '/api/control/audit/history' && request.method === 'GET') {
    const principal = controlPrincipal(request)
    if (!principal)
      return json(response, 401, { error: 'invalid control credential' },
        { 'www-authenticate': 'Bearer realm="graphx-control"' })
    if (!principal.permissions.has('audit:read'))
      return json(response, 403, { error: 'control audit access is not authorized' })
    let query
    try { query = parseHistoryQuery(url.searchParams, configuredHistory, nodeIds, edgeIds) }
    catch (error) { return json(response, 400, { error: error.message }) }
    historyStore.query({ ...query, kind: 'control_audit', excludeControlAudit: false })
      .then(result => json(response, 200, result))
      .catch(error => json(response, error.message.includes('capacity') ? 429 : 503,
        { error: String(error.message).slice(0, 256) },
        error.message.includes('capacity') ? { 'retry-after': '1' } : {}))
    return
  }
  if (url.pathname === '/api/control/audit' && request.method === 'GET') {
    const principal = controlPrincipal(request)
    if (!principal)
      return json(response, 401, { error: 'invalid control credential' },
        { 'www-authenticate': 'Bearer realm="graphx-control"' })
    if (!principal.permissions.has('audit:read'))
      return json(response, 403, { error: 'control audit access is not authorized' })
    const limitText = url.searchParams.get('limit')
    if ([...url.searchParams.keys()].some(key => key !== 'limit') ||
        (limitText != null && !/^[1-9][0-9]{0,2}$/.test(limitText)))
      return json(response, 400, { error: 'audit limit must be an integer from 1 through 999' })
    return json(response, 200, { records: controlPlane.auditRecords(Number(limitText || 100)),
      stats: controlPlane.stats })
  }
  if (url.pathname === '/api/control/commands' && request.method === 'POST') {
    if (!requestOriginAllowed(request))
      return json(response, 403, { accepted: false, error: 'origin not allowed' })
    if (!withinRateLimit(request, 10, 60000, 'control'))
      return json(response, 429, { accepted: false, error: 'control rate limit exceeded' })
    const contentType = String(request.headers['content-type'] || '').split(';', 1)[0]
    if (contentType !== 'application/json')
      return json(response, 415, { accepted: false, error: 'content-type must be application/json' })
    readControlBody(request)
      .then(body => issueControl(request, response, body))
      .catch(error => {
        if (!response.headersSent)
          json(response, error.message.includes('exceeds') ? 413 : 400,
            { accepted: false, error: String(error.message).slice(0, 256) })
      })
    return
  }
  if (/^\/api\/control\/(pause|resume|reset)$/.test(url.pathname) && request.method === 'POST') {
    const action = url.pathname.split('/').pop()
    if (!requestOriginAllowed(request)) return json(response, 403, { accepted: false, action, error: 'origin not allowed' })
    if (!withinRateLimit(request, 10, 60000, 'control')) return json(response, 429, { accepted: false, action, error: 'control rate limit exceeded' })
    if (Number(request.headers['content-length'] || 0) > 0 || request.headers['transfer-encoding'])
      return json(response, 413, { accepted: false, action, error: 'request body not accepted' })
    return issueControl(request, response, { action, targetNodes: null })
  }
  if (url.pathname.startsWith('/api/control/'))
    return json(response, 405, { error: 'method not allowed' }, { allow: 'POST' })
  if (url.pathname.startsWith('/captures/')) {
    if (request.method !== 'GET') return json(response, 405, { error: 'method not allowed' }, { allow: 'GET' })
    let name
    try { name = decodeURIComponent(url.pathname.slice('/captures/'.length)) }
    catch { return json(response, 400, { error: 'invalid capture name' }) }
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}\.pcapng$/.test(name))
      return json(response, 404, { error: 'capture not found' })
    const capturePath = join(captureDirectory, name)
    let descriptor
    try {
      descriptor = openValidatedCapture(capturePath, captureConfig.maxFileBytes,
        captureConfig.maxPackets + 2).descriptor
    } catch {
      if (descriptor != null) closeSync(descriptor)
      return json(response, 404, { error: 'capture not found' })
    }
    response.writeHead(200, { ...securityHeaders, 'cache-control': 'no-store', 'content-type': 'application/vnd.tcpdump.pcap',
      'content-disposition': `attachment; filename="${name}"` })
    return createReadStream(null, { fd: descriptor, autoClose: true }).pipe(response)
  }
  let requested = url.pathname === '/' ? 'index.html' : url.pathname.slice(1)
  const file = resolve(root, requested)
  if ((file !== root && !file.startsWith(`${root}/`)) || !existsSync(file)) requested = 'index.html'
  const fallback = normalize(join(root, requested))
  if (!existsSync(fallback)) return json(response, 404, { error: 'web assets not built' })
  response.writeHead(200, { ...securityHeaders, 'cache-control': fallback.endsWith('index.html') ? 'no-cache' : 'public, max-age=3600',
    'content-type': types[extname(fallback)] || 'application/octet-stream' })
  createReadStream(fallback).pipe(response)
}

const requestHandler = (request, response) => {
  try { return handleRequest(request, response) }
  catch (error) {
    console.error(`telemetry request failed: ${error instanceof Error ? error.message : 'unknown error'}`)
    if (!response.headersSent) return json(response, 500, { error: 'internal server error' })
    response.destroy()
  }
}
  return requestHandler
}
