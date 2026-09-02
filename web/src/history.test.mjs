import assert from 'node:assert/strict'
import { JSDOM } from 'jsdom'
import React, { act } from 'react'
import { createRoot } from 'react-dom/client'
import test from 'node:test'
import { createServer } from 'vite'
import { isCurrentHistoryResponse, mergeHistoryRecords } from './history.mjs'

test('history pages append without duplicates and newest refresh replaces only when requested', () => {
  const newest = [{ id: 5 }, { id: 4 }]
  const older = [{ id: 4 }, { id: 3 }, { id: 2 }]
  assert.deepEqual(mergeHistoryRecords(newest, older, true).map(record => record.id),
    [5, 4, 3, 2])
  assert.deepEqual(mergeHistoryRecords([...newest, ...older], [{ id: 6 }, { id: 5 }], false),
    [{ id: 6 }, { id: 5 }])
})

test('only the latest history request may update console state', () => {
  assert.equal(isCurrentHistoryResponse(3, 4), false)
  assert.equal(isCurrentHistoryResponse(4, 4), true)
})

test('mounted history paging pauses refresh and ignores reordered responses', async context => {
  const vite = await createServer({ appType: 'custom', logLevel: 'silent',
    server: { middlewareMode: true } })
  context.after(() => vite.close())
  const { HistoryPanel } = await vite.ssrLoadModule('/src/components/HistoryPanel.jsx')
  const originalGlobals = new Map(['window', 'document', 'HTMLElement', 'Node', 'fetch']
    .map(name => [name, globalThis[name]]))
  const originalActEnvironment = globalThis.IS_REACT_ACT_ENVIRONMENT
  const dom = new JSDOM('<div id="root"></div>', { url: 'http://graphx.test' })
  globalThis.window = dom.window
  globalThis.document = dom.window.document
  globalThis.HTMLElement = dom.window.HTMLElement
  globalThis.Node = dom.window.Node
  globalThis.IS_REACT_ACT_ENVIRONMENT = true
  const requests = []
  globalThis.fetch = (url, options) => new Promise(resolveRequest => {
    requests.push({ url: String(url), options, resolve: body => resolveRequest(
      new Response(JSON.stringify(body), { status: 200,
        headers: { 'content-type': 'application/json' } })) })
  })
  const root = createRoot(document.getElementById('root'))
  let mounted = true
  const properties = (token, refreshIntervalMs) => ({ observationToken: token,
    refreshIntervalMs, backend: { enabled: true, backend: 'sqlite', status: 'ready' } })
  const button = label => [...document.querySelectorAll('button')]
    .find(value => value.textContent === label)
  const click = target => target.dispatchEvent(new dom.window.MouseEvent('click',
    { bubbles: true }))
  try {
    await act(async () => { root.render(
      React.createElement(HistoryPanel, properties('observation-token-one', 1000))) })
    assert.equal(requests.length, 1)
    await act(async () => requests[0].resolve({ records: [
      { id: 5, recordedAt: new Date().toISOString(), kind: 'trace', event: 'send',
        data: { sequence: 5 } },
      { id: 4, recordedAt: new Date().toISOString(), kind: 'trace', event: 'send',
        data: { sequence: 4 } }], nextCursor: 4 }))

    await act(async () => { root.render(
      React.createElement(HistoryPanel, properties('observation-token-one', 20))) })
    await act(async () => click(button('Load older records')))
    assert.match(requests[1].url, /cursor=4/)
    await act(async () => requests[1].resolve({ records: [
      { id: 3, recordedAt: new Date().toISOString(), kind: 'trace', event: 'send',
        data: { sequence: 3 } }], nextCursor: 3 }))
    await act(async () => new Promise(resolveWait => setTimeout(resolveWait, 60)))
    assert.equal(requests.length, 2, 'polling must remain paused on older pages')

    await act(async () => { root.render(
      React.createElement(HistoryPanel, properties('observation-token-one', 1000))) })
    await act(async () => click(button('Return to newest')))
    assert.equal(requests.length, 3)
    await act(async () => { root.render(
      React.createElement(HistoryPanel, properties('observation-token-two', 1000))) })
    assert.equal(requests.length, 4)
    await act(async () => requests[3].resolve({ records: [
      { id: 7, recordedAt: new Date().toISOString(), kind: 'trace', event: 'send',
        data: { sequence: 7 } }], nextCursor: null }))
    await act(async () => requests[2].resolve({ records: [
      { id: 6, recordedAt: new Date().toISOString(), kind: 'trace', event: 'send',
        data: { sequence: 6 } }], nextCursor: null }))
    assert.match(document.body.textContent, /sequence":7/)
    assert.doesNotMatch(document.body.textContent, /sequence":6/)
    await act(async () => { root.render(
      React.createElement(HistoryPanel, properties('observation-token-three', 1000))) })
    assert.equal(requests.length, 5)
    await act(async () => root.unmount())
    mounted = false
    await act(async () => requests[4].resolve({ records: [
      { id: 8, recordedAt: new Date().toISOString(), kind: 'trace', event: 'send',
        data: { sequence: 8 } }], nextCursor: null }))
  } finally {
    if (mounted) await act(async () => root.unmount())
    dom.window.close()
    for (const [name, value] of originalGlobals)
      if (value === undefined) delete globalThis[name]
      else globalThis[name] = value
    globalThis.IS_REACT_ACT_ENVIRONMENT = originalActEnvironment
  }
})
