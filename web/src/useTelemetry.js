import { useEffect, useState } from 'react'

export function useTelemetry() {
  const [snapshot, setSnapshot] = useState(null)
  const [connected, setConnected] = useState(false)

  useEffect(() => {
    let socket
    let retry
    let stopped = false
    let attempts = 0
    const connect = (path = '/ws') => {
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
      socket = new WebSocket(`${protocol}://${location.host}${path}`)
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
    fetch('/api/topology').then(response => response.json()).then(initial => {
      if (stopped) return
      setSnapshot(initial)
      connect(initial.telemetry?.websocket || '/ws')
    }).catch(() => connect())
    return () => { stopped = true; clearTimeout(retry); socket?.close() }
  }, [])

  return { snapshot, connected }
}
