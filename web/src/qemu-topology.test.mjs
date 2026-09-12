import assert from 'node:assert/strict'
import test from 'node:test'
import { applicationEdges, applicationNodes, infrastructureNodes } from './data/topology.js'

test('QEMU raw topology preserves deployment and observation metadata', () => {
  const topology = {
    nodes: [{ id: 'qemu-node', label: 'qemu-node', role: 'virtual-machine',
      image: 'local process', runtime: 'qemu', execution: 'host',
      lifecycle: 'external', control: 'none', accelerator: 'tcg',
      requestedAccelerator: 'tcg', selectedAccelerator: 'tcg', actualAccelerator: 'tcg',
      acceleratorEvidence: 'QMP query-status + query-kvm',
      vmState: 'running', guestState: 'ready', guestProtocols: { tcp: true, udp: true },
      guestArchitecture: 'x86_64', input: true, output: true }],
    edges: [{ id: 'peer-qemu-udp', source: 'host-peer', target: 'qemu-node',
      transport: 'udp', dataPlane: 'external', framing: 'none',
      observationSource: 'qemu-pcap', port: 18001, schema: 'RawUdpDatagram' }],
  }
  const [node] = applicationNodes(topology)
  assert.equal(node.data.runtime, 'qemu')
  assert.equal(node.data.execution, 'host')
  assert.equal(node.data.control, 'none')
  assert.equal(node.data.accelerator, 'tcg')
  assert.equal(node.data.requestedAccelerator, 'tcg')
  assert.equal(node.data.actualAccelerator, 'tcg')
  assert.equal(node.data.guestArchitecture, 'x86_64')
  assert.equal(node.data.vmState, 'running')
  assert.equal(node.data.guestState, 'ready')
  assert.deepEqual(node.data.guestProtocols, { tcp: true, udp: true })
  const [edge] = applicationEdges(topology)
  assert.equal(edge.data.framing, 'none')
  assert.equal(edge.data.dataPlane, 'external')
  assert.equal(edge.data.observationSource, 'qemu-pcap')
  const [networkNode] = infrastructureNodes({ networkNodes: topology.nodes })
  assert.equal(networkNode.data.runtime, 'qemu')
  assert.equal(networkNode.data.execution, 'host')
})

test('QEMU deployment hierarchy remains distinct in the network view', () => {
  const topology = { networkNodes: [
    { id: 'qemu-node-host-runtime', label: 'Host QEMU process', role: 'Host runtime',
      hierarchy: 'host', runtimeLayer: 'boundary', status: 'running', input: true, output: true },
    { id: 'qemu-node', label: 'qemu-node', role: 'virtual-machine', hierarchy: 'virtual-machine',
      parent: 'qemu-node-host-runtime', runtimeLayer: 'vm', status: 'paused', vmState: 'paused',
      input: true, output: true },
    { id: 'qemu-node-guest-app', label: 'guest application', role: 'Guest application',
      hierarchy: 'guest', parent: 'qemu-node', runtimeLayer: 'guest', status: 'unavailable',
      guestState: 'unavailable', guestProtocols: { tcp: false, udp: false }, input: true, output: true },
  ] }
  const nodes = infrastructureNodes(topology)
  assert.deepEqual(nodes.map(node => node.data.hierarchy), ['host', 'virtual-machine', 'guest'])
  assert.equal(nodes[1].data.parent, 'qemu-node-host-runtime')
  assert.equal(nodes[2].data.parent, 'qemu-node')
  assert.deepEqual(nodes.map(node => node.data.status), ['running', 'paused', 'unavailable'])
  assert.equal(nodes[1].data.vmState, 'paused')
  assert.deepEqual(nodes[2].data.guestProtocols, { tcp: false, udp: false })
})

test('QEMU deployment state transitions remain layer-specific', () => {
  const states = [
    ['booting', 'probing'],
    ['running', 'ready'],
    ['running', 'unavailable'],
    ['running', 'ready'],
    ['stopped', 'stopped'],
  ]
  for (const [vmState, guestState] of states) {
    const nodes = infrastructureNodes({ networkNodes: [
      { id: 'qemu-node', label: 'VM', role: 'virtual-machine', runtimeLayer: 'vm',
        status: vmState, vmState, input: true, output: true },
      { id: 'qemu-node-guest-app', label: 'guest', role: 'Guest application', runtimeLayer: 'guest',
        status: guestState, guestState, guestProtocols: { tcp: guestState === 'ready', udp: guestState === 'ready' },
        input: true, output: true },
    ] })
    assert.equal(nodes[0].data.status, vmState)
    assert.equal(nodes[0].data.vmState, vmState)
    assert.equal(nodes[1].data.status, guestState)
    assert.equal(nodes[1].data.guestState, guestState)
    assert.equal(nodes[1].data.guestProtocols.tcp, guestState === 'ready')
    assert.equal(nodes[1].data.guestProtocols.udp, guestState === 'ready')
  }
})

test('SDR raw topology preserves external ownership, OVS, and control metadata', () => {
  const topology = {
    nodes: [
      { id: 'sdr-node', label: 'sdr-node', role: 'radio', runtime: 'external',
        execution: 'host', lifecycle: 'external', control: 'none', input: true, output: true },
      { id: 'processor', label: 'processor', role: 'transform', runtime: 'docker',
        execution: 'container', lifecycle: 'managed', control: 'origin', input: true, output: true },
    ],
    edges: [{ id: 'sdr-samples', source: 'sdr-node', target: 'processor', transport: 'udp',
      dataPlane: 'external', framing: 'none', observationSource: 'ovs-span', port: 18400,
      schema: 'RawIqSamples' }],
    networkNodes: [{ id: 'br-sdr', label: 'br-sdr', role: 'openvswitch', kind: 'openvswitch',
      hierarchy: 'infrastructure', input: true, output: true }],
  }
  const nodes = applicationNodes(topology)
  assert.equal(nodes[0].data.runtime, 'external')
  assert.equal(nodes[0].data.lifecycle, 'external')
  assert.equal(nodes[1].data.control, 'origin')
  const [edge] = applicationEdges(topology)
  assert.equal(edge.data.dataPlane, 'external')
  assert.equal(edge.data.framing, 'none')
  assert.equal(edge.data.observationSource, 'ovs-span')
  assert.equal(infrastructureNodes(topology)[0].data.role, 'openvswitch')
})
