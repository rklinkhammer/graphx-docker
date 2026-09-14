import { useEffect, useRef, useState } from 'react'
import xterm from '@xterm/xterm'
import '@xterm/xterm/css/xterm.css'
import { bearerHeaders } from '../auth'
import { decodeHex, logText, serialDelta } from '../nodeConsole.mjs'

export function NodeConsolePanel({ node, observationToken, controlToken, hasControl }) {
  const [view, setView] = useState('logs')
  const [status, setStatus] = useState('Connecting…')
  const [snapshot, setSnapshot] = useState(null)
  const [search, setSearch] = useState('')
  const [paused, setPaused] = useState(false)
  const [writer, setWriter] = useState(null)
  const [gap, setGap] = useState('')
  const host = useRef(null), logs = useRef(null), terminal = useRef(null), current = useRef(null), lease = useRef(null)
  const requestRef = useRef(null)
  const isQemu = node.data?.runtime === 'qemu' || node.data?.execution?.kind === 'qemu'
  const command = async (action, extra = {}, keepalive = false) => {
    const response = await fetch(`/api/control/serial/${encodeURIComponent(node.id)}`, {
      method: 'POST', keepalive, signal: keepalive ? undefined : AbortSignal.timeout(5000),
      headers: { ...bearerHeaders(controlToken), 'Content-Type': 'application/json' },
      body: JSON.stringify({ action, generation: current.current?.generation, lease: lease.current, ...extra }),
    })
    const value = await response.json()
    if (!response.ok) throw new Error(value.error || 'Serial request failed')
    if (action === 'acquire') lease.current = value.lease
    if (action === 'release') lease.current = null
    return value
  }
  requestRef.current = command
  useEffect(() => {
    let active = true, timer
    current.current = null; lease.current = null
    setSnapshot(null); setWriter(null); setGap(''); setStatus('Connecting…')
    const abort = new AbortController()
    const poll = async () => {
      try {
        const response = await fetch(`/api/nodes/${encodeURIComponent(node.id)}/${view}`, {
          headers: bearerHeaders(observationToken), signal: AbortSignal.any([abort.signal, AbortSignal.timeout(5000)]),
        })
        const value = await response.json()
        if (!active) return
        if (!response.ok) { lease.current = null; throw new Error(value.error || `Console returned ${response.status}`) }
        if (view === 'serial') {
          const delta = serialDelta(current.current, value)
          if (delta.reset) { terminal.current?.reset(); lease.current = null }
          if (delta.gap) terminal.current?.writeln('\r\n[Output gap: retained serial buffer exceeded]\r\n')
          if (terminal.current && delta.bytes.length) await new Promise(resolve => terminal.current.write(delta.bytes, resolve))
          setWriter(value.writer)
          if (!value.writer) lease.current = null
          if (lease.current) await requestRef.current('renew')
        }
        if (view === 'logs' && current.current && (value.generation !== current.current.generation ||
            value.runtime !== current.current.runtime || !value.hex.startsWith(current.current.hex)))
          setGap('Output window moved or the node restarted; earlier output may be omitted.')
        current.current = value
        setSnapshot(value)
        setStatus(value.stale ? 'Disconnected: log relay is stale' : value.status)
      } catch (error) { if (active) { setStatus(error.message); setGap('Connection interrupted; output outside the retained window may be missing.'); lease.current = null } }
      if (active) timer = setTimeout(poll, 1500)
    }
    void poll()
    const leave = () => { if (lease.current) void requestRef.current('release', {}, true).catch(() => {}) }
    window.addEventListener('pagehide', leave)
    return () => { leave(); active = false; abort.abort(); clearTimeout(timer); window.removeEventListener('pagehide', leave) }
  }, [node.id, view, observationToken, controlToken])
  useEffect(() => {
    if (view !== 'serial') return
    const term = new xterm.Terminal({ cols: 80, rows: 24, scrollback: 1000, fontSize: 12, disableStdin: false,
      theme: { background: '#090f1a' }, linkHandler: { activate() {} } })
    terminal.current = term
    term.open(host.current)
    // Do not permit guest-controlled clipboard or title operations.
    const handlers = [0, 1, 2, 8, 52].map(code => term.parser.registerOscHandler(code, () => true))
    let pending = '', busy = false
    const timer = setInterval(async () => {
      if (!lease.current) { pending = ''; return }
      if (busy || !pending) return
      const hex = pending; pending = ''; busy = true
      try { await requestRef.current('input', { hex }) }
      catch (error) { lease.current = null; setStatus(error.message) }
      finally { busy = false }
    }, 500)
    const input = term.onData(data => {
      if (!lease.current) return
      const hex = Array.from(new TextEncoder().encode(data), byte => byte.toString(16).padStart(2, '0')).join('')
      if (pending.length + hex.length > 8192) { setStatus('Input exceeds 4 KiB; paste a smaller selection'); return }
      pending += hex
    })
    return () => { clearInterval(timer); input.dispose(); handlers.forEach(handler => handler.dispose()); term.dispose(); terminal.current = null }
  }, [node.id, view])
  useEffect(() => { if (!paused && logs.current) logs.current.scrollTop = logs.current.scrollHeight }, [snapshot, paused])
  const text = snapshot && view === 'logs' ? logText(snapshot) : ''
  const download = () => {
    if (!snapshot) return
    const url = URL.createObjectURL(new Blob([decodeHex(snapshot.hex)], { type: 'text/plain' }))
    const link = document.createElement('a'); link.href = url; link.download = `${node.id}-${view}.log`; link.click(); URL.revokeObjectURL(url)
  }
  return <aside className="inspector node-console">
    <div className="eyebrow">NODE CONSOLE</div><h2>{node.id}</h2>
    <div className="console-tools"><button onClick={() => setView('logs')}>Logs</button>
      {isQemu && <button onClick={() => setView('serial')}>Serial</button>}
      <button disabled={!snapshot} onClick={download}>Download retained output</button></div>
    <p role="status">{status}</p>{gap && <p role="note">{gap}</p>}
    {view === 'logs' ? <>
      <p>{snapshot?.source} · Last 64 KiB; older output may be omitted.</p>
      <p>Generation: {snapshot?.generation || 'unavailable'} · Runtime: {snapshot?.runtime || 'unavailable'}</p>
      <label>Search loaded output <input value={search} onChange={event => setSearch(event.target.value)}/></label>
      <button onClick={() => setPaused(!paused)}>{paused ? 'Resume scrolling' : 'Pause scrolling'}</button>
      <pre ref={logs} className="console-log">{search ? text.split('\n').filter(line => line.includes(search)).join('\n') : text}</pre>
      {snapshot?.diagnostics_hex && <details><summary>QEMU process errors</summary><pre className="console-log">{logText({ hex: snapshot.diagnostics_hex })}</pre></details>}
    </> : <>
      <p>Guest ttyS1 · A login service must be supplied by the guest image. Fixed 80 × 24 terminal.</p>
      <p>{writer ? `Writer: ${writer.actor}` : 'Read-only; no writer'}</p>
      <button disabled={!hasControl || !snapshot || Boolean(writer)} onClick={() => command('acquire').then(() => { setWriter({ actor: 'you' }); terminal.current?.focus() }).catch(error => setStatus(error.message))}>Acquire keyboard</button>
      <button disabled={!lease.current} onClick={() => command('release').then(() => setWriter(null)).catch(error => setStatus(error.message))}>Release keyboard</button>
      <div className="serial-terminal" ref={host}/>
    </>}
  </aside>
}
