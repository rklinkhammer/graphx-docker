import assert from 'node:assert/strict'
import test from 'node:test'
import { positionedNodes } from './topologyLayout.mjs'

test('nodes awaiting asynchronous layout always have an initial React Flow position', () => {
  const nodes = positionedNodes([
    { id: 'source', data: {} },
    { id: 'sink', data: {}, position: { x: 12, y: 34 } },
  ])

  assert.deepEqual(nodes[0].position, { x: 0, y: 80 })
  assert.deepEqual(nodes[1].position, { x: 12, y: 34 })
  assert.equal(nodes[0].type, 'graphx')
})
