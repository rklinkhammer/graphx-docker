import { useEffect, useState } from 'react'
import { controlStatusRequest } from '../auth'

const TERMINAL = new Set(['accepted', 'rejected', 'timed-out'])

export function formatCommandStatus(action, command) {
  const detail = command.error || command.message
  return `${action} ${command.status} · ${command.id.slice(0, 8)}` +
    (detail ? ` · ${detail}` : '')
}

export function ControlCommandStatus({ command, token, fallback, pollIntervalMs = 250,
  timeoutMs = 40_000, requestTimeoutMs = 2_000, fetcher = fetch }) {
  const [message, setMessage] = useState(fallback)

  useEffect(() => {
    if (!command) { setMessage(fallback); return undefined }
    setMessage(formatCommandStatus(command.action, command))
    if (TERMINAL.has(command.status)) return undefined

    let cancelled = false
    let timer
    let request
    const started = Date.now()
    const poll = async () => {
      request = new AbortController()
      const requestTimer = setTimeout(() => request.abort(), requestTimeoutMs)
      try {
        const descriptor = controlStatusRequest(command.id, token)
        const response = await fetcher(descriptor.url,
          { ...descriptor.options, signal: request.signal })
        const result = await response.json()
        if (cancelled) return
        if (!response.ok || !result?.id) {
          setMessage(result.error || 'Control command status is unavailable')
          return
        }
        setMessage(formatCommandStatus(command.action, result))
        if (TERMINAL.has(result.status)) return
      } catch (error) {
        if (!cancelled)
          setMessage('Control command status is unavailable')
        return
      } finally { clearTimeout(requestTimer) }
      if (!cancelled && Date.now() - started < timeoutMs)
        timer = setTimeout(poll, pollIntervalMs)
      else if (!cancelled) setMessage('Control command status timed out')
    }
    timer = setTimeout(poll, pollIntervalMs)
    return () => {
      cancelled = true
      clearTimeout(timer)
      request?.abort()
    }
  }, [command, token, pollIntervalMs, timeoutMs, requestTimeoutMs, fetcher, fallback])

  return <span>{message}</span>
}
