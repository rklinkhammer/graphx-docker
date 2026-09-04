export function positionedNodes(nodes) {
  return nodes.map((node, index) => ({
    ...node,
    type: 'graphx',
    position: node.position || { x: index * 240, y: 80 },
  }))
}
