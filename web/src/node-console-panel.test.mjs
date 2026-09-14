import assert from 'node:assert/strict'
import { JSDOM } from 'jsdom'
import React, { act } from 'react'
import { createRoot } from 'react-dom/client'
import test from 'node:test'
import { createServer } from 'vite'

test('mounted node console shows logs as text, reports denied access and aborts on selection change', async context => {
  const vite = await createServer({ appType: 'custom', logLevel: 'silent', server: { middlewareMode: true } })
  context.after(() => vite.close())
  const { NodeConsolePanel } = await vite.ssrLoadModule('/src/components/NodeConsolePanel.jsx')
  const names = ['window', 'document', 'HTMLElement', 'Node', 'fetch', 'IS_REACT_ACT_ENVIRONMENT']
  const originals = new Map(names.map(name => [name, globalThis[name]]))
  const dom = new JSDOM('<div id="root"></div>', { url: 'http://graphx.test' })
  Object.assign(globalThis, { window: dom.window, document: dom.window.document,
    HTMLElement: dom.window.HTMLElement, Node: dom.window.Node, IS_REACT_ACT_ENVIRONMENT: true })
  const calls = []
  globalThis.fetch = async (url, options) => {
    calls.push({ url, options })
    return new Response(JSON.stringify(url.includes('/denied/') ? { error: 'observation credential required' } : {
      hex: Buffer.from('<script>untrusted output</script>\nready').toString('hex'), source: 'stdout/stderr (merged)',
      generation: 'test-generation', runtime: '42:start', status: 'running',
    }), { status: url.includes('/denied/') ? 401 : 200, headers: { 'Content-Type': 'application/json' } })
  }
  const root = createRoot(document.getElementById('root'))
  try {
    await act(async () => root.render(React.createElement(NodeConsolePanel, {
      node: { id: 'worker', data: { runtime: 'native' } }, observationToken: 'read-token', hasControl: false,
    })))
    assert.match(document.body.textContent, /untrusted output/)
    assert.equal(document.querySelectorAll('script').length, 0)
    assert.equal(calls[0].options.headers.Authorization, 'Bearer read-token')
    assert.match(document.body.textContent, /test-generation/)
    await act(async () => root.render(React.createElement(NodeConsolePanel, {
      node: { id: 'denied', data: { runtime: 'native' } }, observationToken: 'invalid', hasControl: false,
    })))
    assert.equal(calls[0].options.signal.aborted, true)
    assert.match(document.body.textContent, /observation credential required/)
    assert.doesNotMatch(document.body.textContent, /untrusted output/)
  } finally {
    await act(async () => root.unmount())
    dom.window.close()
    for (const [name, value] of originals) { if (value === undefined) delete globalThis[name]; else globalThis[name] = value }
  }
})
