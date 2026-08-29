import { useEffect, useState } from 'react'

export function useTelemetry() {
  const [snapshot, setSnapshot] = useState(null)
  const [connected, setConnected] = useState(false)

  useEffect(() => {
    let socket
    let retry
    let stopped = false
    let attempts = 0
    const connect = () => {
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
      socket = new WebSocket(`${protocol}://${location.host}/ws`)
      socket.onopen = () => { attempts = 0; setConnected(true) }
      socket.onmessage = event => {
        try { setSnapshot(JSON.parse(event.data)) } catch { /* Ignore malformed snapshots. */ }
      }
      socket.onerror = () => socket.close()
      socket.onclose = () => {
        setConnected(false)
        if (!stopped) retry = setTimeout(connect, Math.min(1000 * 2 ** attempts++, 10000))
      }
    }
    connect()
    return () => { stopped = true; clearTimeout(retry); socket?.close() }
  }, [])

  return { snapshot, connected }
}
