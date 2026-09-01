import { useEffect, useState } from 'react'
import { bearerHeaders, webSocketProtocols } from './auth'

export function useTelemetry(observationToken = '') {
  const [snapshot, setSnapshot] = useState(null)
  const [connected, setConnected] = useState(false)

  useEffect(() => {
    let socket
    let retry
    let stopped = false
    let attempts = 0
    const connect = (path = '/ws') => {
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
      const protocols = webSocketProtocols(observationToken)
      socket = new WebSocket(`${protocol}://${location.host}${path}`, protocols)
      socket.onopen = () => { attempts = 0; setConnected(true) }
      socket.onmessage = event => {
        try { setSnapshot(JSON.parse(event.data)) } catch { /* Ignore malformed snapshots. */ }
      }
      socket.onerror = () => socket.close()
      socket.onclose = () => {
        setConnected(false)
        if (!stopped) retry = setTimeout(() => connect(path), Math.min(1000 * 2 ** attempts++, 10000))
      }
    }
    const headers = bearerHeaders(observationToken)
    fetch('/api/topology', { headers }).then(response => {
      if (!response.ok) throw new Error(`telemetry returned ${response.status}`)
      return response.json()
    }).then(initial => {
      if (stopped) return
      setSnapshot(initial)
      connect(initial.telemetry?.websocket || '/ws')
    }).catch(() => connect())
    return () => { stopped = true; clearTimeout(retry); socket?.close() }
  }, [observationToken])

  return { snapshot, connected }
}
