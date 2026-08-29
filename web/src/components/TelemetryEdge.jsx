import { BaseEdge, EdgeLabelRenderer, getBezierPath } from '@xyflow/react'

export function TelemetryEdge({ id, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, data, markerEnd, selected }) {
  const [path, labelX, labelY] = getBezierPath({ sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition })
  return <>
    <BaseEdge id={id} path={path} markerEnd={markerEnd} className={selected || data.highlighted ? 'edge-selected' : ''} />
    <EdgeLabelRenderer>
      <div className={`edge-badge ${selected || data.highlighted ? 'selected' : ''}`} style={{ transform: `translate(-50%, -50%) translate(${labelX}px,${labelY}px)` }}>
        <strong>{data.rate}</strong><span>{data.latency}</span>
      </div>
    </EdgeLabelRenderer>
  </>
}
