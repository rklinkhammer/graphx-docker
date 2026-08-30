import { Handle, Position } from '@xyflow/react'
import { Box, Cpu } from 'lucide-react'

export function NodeCard({ data, selected }) {
  const cpu = Number.isFinite(data.cpu)
    ? `${data.cpu < 1 ? data.cpu.toFixed(2) : data.cpu.toFixed(1)}%`
    : '—'
  return <div className={`node-card ${selected ? 'selected' : ''}`}>
    {data.input && <Handle type="target" position={Position.Left} />}
    <div className="node-topline"><span className="role">{data.role}</span><span className={`status ${data.status}`}>{data.status}</span></div>
    <div className="node-title"><Box size={17} />{data.label}</div>
    <div className="node-meta"><span><Cpu size={13} /> CPU {cpu}</span><span>{data.image}</span></div>
    {data.output && <Handle type="source" position={Position.Right} />}
  </div>
}
