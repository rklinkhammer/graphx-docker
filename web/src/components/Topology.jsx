import { useEffect, useState } from 'react'
import ELK from 'elkjs/lib/elk.bundled.js'
import { Background, Controls, MiniMap, ReactFlow, useNodesState } from '@xyflow/react'
import { NodeCard } from './NodeCard'
import { TelemetryEdge } from './TelemetryEdge'

const nodeTypes = { graphx: NodeCard }
const edgeTypes = { telemetry: TelemetryEdge }

export function Topology({ nodes, edges, onEdgeSelect }) {
  const [layoutNodes, setLayoutNodes, onNodesChange] = useNodesState(nodes.map(n => ({ ...n, type: 'graphx' })))
  const [flow, setFlow] = useState(null)
  const topologyKey = `${nodes.map(node => node.id).join(',')}|${edges.map(edge => edge.id).join(',')}`

  useEffect(() => {
    const elk = new ELK()
    elk.layout({ id: 'root', layoutOptions: { 'elk.algorithm': 'layered', 'elk.direction': 'RIGHT', 'elk.spacing.nodeNode': '90' },
      children: nodes.map(node => ({ id: node.id, width: 205, height: 115 })),
      edges: edges.map(edge => ({ id: edge.id, sources: [edge.source], targets: [edge.target] }))
    }).then(graph => {
      setLayoutNodes(nodes.map(node => {
        const position = graph.children.find(item => item.id === node.id)
        return { ...node, type: 'graphx', position: position ? { x: position.x, y: position.y } : node.position }
      }))
      setTimeout(() => flow?.fitView({ padding: 0.12, duration: 250 }), 0)
    })
  }, [topologyKey, flow])

  useEffect(() => {
    setLayoutNodes(current => current.map(node => {
      const latest = nodes.find(item => item.id === node.id)
      return latest ? { ...node, data: latest.data } : node
    }))
  }, [nodes, setLayoutNodes])

  return <div className="topology-canvas">
    <ReactFlow nodes={layoutNodes} edges={edges} onNodesChange={onNodesChange} nodeTypes={nodeTypes} edgeTypes={edgeTypes}
      onInit={setFlow} onEdgeClick={(_, edge) => onEdgeSelect(edge.data.logicalEdge || edge.id)} fitView minZoom={0.25} maxZoom={1.5} nodesDraggable>
      <Background color="#253245" gap={24} size={1}/><Controls showInteractive={false}/>
      <MiniMap pannable zoomable nodeColor="#3b82f6" maskColor="rgba(5,10,18,.72)"/>
    </ReactFlow>
  </div>
}
