import { Download } from 'lucide-react'
import { bearerHeaders } from '../auth'

function formatBytes(value) {
  if (!Number.isFinite(value)) return '—'
  if (value < 1024) return `${value} B`
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KiB`
  return `${(value / (1024 * 1024)).toFixed(1)} MiB`
}

export function CapturePanel({ capture, observationToken }) {
  const files = capture?.files || []
  const download = async file => {
    const response = await fetch(file.url, { headers: bearerHeaders(observationToken) })
    if (!response.ok) return
    const objectUrl = URL.createObjectURL(await response.blob())
    const link = document.createElement('a')
    link.href = objectUrl
    link.download = file.name
    link.click()
    URL.revokeObjectURL(objectUrl)
  }
  return <section className="history-panel">
    <div className="history-heading"><div><span>BOUNDED CAPTURE CATALOG</span><h2>Packet captures</h2></div>
      <div className={`history-state ${capture?.enabled ? 'ready' : 'disabled'}`}>
        {capture?.enabled ? `${capture.provider} · ready` : 'capture disabled'}
      </div></div>
    <div className="history-stats">
      <span><b>{files.length}</b> files</span>
      <span><b>{formatBytes(capture?.limits?.maxFileBytes)}</b> file limit</span>
      <span><b>{capture?.limits?.maxPackets?.toLocaleString?.() || 0}</b> packet limit</span>
      <span><b>{capture?.catalogTruncated ? 'yes' : 'no'}</b> catalog truncated</span>
    </div>
    {!files.length && <div className="history-message">No capture files are available yet.</div>}
    {files.length > 0 && <div className="history-table" role="table">
      <div className="history-row capture-row history-header" role="row"><span>Modified</span><span>Format</span><span>Node</span><span>Link type</span><span>Size</span><span>Action</span></div>
      {files.map(file => <div className="history-row capture-row" role="row" key={file.url}>
        <span>{new Date(file.modifiedAt).toLocaleString()}</span><span>{file.format}</span><span>{file.nodeId}</span>
        <span>{file.linkType}</span><span>{formatBytes(file.size)}</span>
        <span><button className="history-more capture-download" onClick={() => download(file)}><Download size={13}/> Download {file.format === 'ethernet' ? 'Ethernet' : 'GraphX'}</button></span>
      </div>)}
    </div>}
  </section>
}
