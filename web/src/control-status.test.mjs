import assert from 'node:assert/strict'
import { JSDOM } from 'jsdom'
import React, { act } from 'react'
import { createRoot } from 'react-dom/client'
import test from 'node:test'
import { createServer } from 'vite'

test('mounted control status reconciles pending commands to terminal state and stops polling', async context => {
  const vite = await createServer({ appType: 'custom', logLevel: 'silent',
    server: { middlewareMode: true } })
  context.after(() => vite.close())
  const { ControlCommandStatus } = await vite.ssrLoadModule('/src/components/ControlCommandStatus.jsx')
  const originalGlobals = new Map(['window', 'document', 'HTMLElement', 'Node']
    .map(name => [name, globalThis[name]]))
  const originalActEnvironment = globalThis.IS_REACT_ACT_ENVIRONMENT
  const dom = new JSDOM('<div id="root"></div>', { url: 'http://graphx.test' })
  globalThis.window = dom.window
  globalThis.document = dom.window.document
  globalThis.HTMLElement = dom.window.HTMLElement
  globalThis.Node = dom.window.Node
  globalThis.IS_REACT_ACT_ENVIRONMENT = true
  const requests = []
  let terminal = 'accepted'
  let requestInCycle = 0
  const fetcher = async (url, options) => {
    requests.push({ url, options })
    const status = requestInCycle++ === 0 ? 'pending' : terminal
    return new Response(JSON.stringify({ id: `command-${terminal}`, status }),
      { status: 200, headers: { 'content-type': 'application/json' } })
  }
  const root = createRoot(document.getElementById('root'))
  try {
    for (terminal of ['accepted', 'rejected', 'timed-out']) {
      requestInCycle = 0
      const before = requests.length
      const commandId = `command-${terminal}`
      await act(async () => root.render(React.createElement(ControlCommandStatus, {
        command: { id: commandId, status: 'pending', action: 'pause' },
        token: 'control-token', fallback: 'idle', pollIntervalMs: 5, fetcher,
      })))
      await act(async () => new Promise(resolve => setTimeout(resolve, 40)))
      assert.match(document.body.textContent, new RegExp(`pause ${terminal} · command-`))
      assert.equal(requests.length, before + 2)
      assert.equal(requests[before].url, `/api/control/commands/${commandId}`)
      assert.equal(requests[before].options.headers.Authorization, 'Bearer control-token')
      await act(async () => new Promise(resolve => setTimeout(resolve, 20)))
      assert.equal(requests.length, before + 2, 'terminal state must stop polling')
    }
    const stalledFetcher = (url, options) => new Promise((resolve, reject) => {
      options.signal.addEventListener('abort', () => reject(
        Object.assign(new Error('aborted'), { name: 'AbortError' })))
    })
    await act(async () => root.render(React.createElement(ControlCommandStatus, {
      command: { id: 'command-stalled', status: 'pending', action: 'resume' },
      token: 'control-token', fallback: 'idle', pollIntervalMs: 5,
      requestTimeoutMs: 5, fetcher: stalledFetcher,
    })))
    await act(async () => new Promise(resolve => setTimeout(resolve, 25)))
    assert.match(document.body.textContent, /status is unavailable/)
  } finally {
    await act(async () => root.unmount())
    dom.window.close()
    for (const [name, value] of originalGlobals)
      if (value === undefined) delete globalThis[name]
      else globalThis[name] = value
    globalThis.IS_REACT_ACT_ENVIRONMENT = originalActEnvironment
  }
})
