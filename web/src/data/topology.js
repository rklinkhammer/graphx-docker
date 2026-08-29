export const initialNodes = [
  { id: 'generator', position: { x: 40, y: 120 }, data: { label: 'Generator', role: 'Source', status: 'healthy', cpu: 3, image: 'graphx-demo:latest', input: false, output: true } },
  { id: 'transform', position: { x: 350, y: 120 }, data: { label: 'Transform', role: 'Processor', status: 'healthy', cpu: 17, image: 'graphx-demo:latest', input: true, output: true } },
  { id: 'sink', position: { x: 660, y: 120 }, data: { label: 'Sink', role: 'Consumer', status: 'healthy', cpu: 6, image: 'graphx-demo:latest', input: true, output: false } },
]

export const initialEdges = [
  { id: 'samples', source: 'generator', target: 'transform', type: 'telemetry', data: { label: 'samples', rate: '4.8 MB/s', messages: '18.2k/s', latency: '12.4 µs', drops: 0, port: 7001, schema: 'Sample' } },
  { id: 'transformed', source: 'transform', target: 'sink', type: 'telemetry', data: { label: 'transformed', rate: '2.3 MB/s', messages: '9.1k/s', latency: '18.7 µs', drops: 0, port: 7002, schema: 'TransformedSample' } },
]
