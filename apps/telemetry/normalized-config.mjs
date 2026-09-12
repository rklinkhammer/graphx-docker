import { createHash } from 'node:crypto'
import { closeSync, constants, fstatSync, openSync, readSync } from 'node:fs'
import { dirname, normalize } from 'node:path'

export const MAX_NORMALIZED_CONFIG_BYTES = 4 * 1024 * 1024

function fail(message) {
  throw new Error(`GraphX telemetry configuration: ${message}`)
}

function readBoundedRegularFile(path, maximum) {
  let descriptor
  try {
    descriptor = openSync(path, constants.O_RDONLY | (constants.O_NOFOLLOW || 0))
    const metadata = fstatSync(descriptor)
    if (!metadata.isFile()) fail(`${path} is not a regular file`)
    if (metadata.size > maximum) fail(`${path} exceeds the ${maximum}-byte limit`)
    const buffer = Buffer.alloc(metadata.size)
    let offset = 0
    while (offset < buffer.length) {
      const count = readSync(descriptor, buffer, offset, buffer.length - offset, null)
      if (count === 0) break
      offset += count
    }
    if (offset !== metadata.size) fail(`${path} changed while it was being read`)
    return buffer.toString('utf8')
  } catch (error) {
    if (error?.message?.startsWith('GraphX telemetry configuration:')) throw error
    fail(`cannot read ${path}: ${error.message}`)
  } finally {
    if (descriptor != null) closeSync(descriptor)
  }
}

function object(value, path) {
  if (value == null || typeof value !== 'object' || Array.isArray(value))
    fail(`${path} must be an object`)
  return value
}

function array(value, path, maximum) {
  if (!Array.isArray(value)) fail(`${path} must be an array`)
  if (value.length > maximum) fail(`${path} exceeds ${maximum} entries`)
  return value
}

function text(value, path) {
  if (typeof value !== 'string' || value.length === 0 || value.length > 4096)
    fail(`${path} must be a non-empty bounded string`)
  return value
}

function identity(value, path) {
  if (typeof value !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,63}$(?![\s\S])/.test(value))
    fail(`${path} must be a 1–64 byte identifier`)
  return value
}

function validateSdr(node, graph, instanceId) {
  const path = `graph.nodes.${node.id}.sdr`
  identity(instanceId, 'deployment.instance_id')
  const sdr = object(node.sdr, path)
  const fields = ['samples_edge', 'control_edge', 'frequency_hz', 'sample_interval_ms', 'credentials']
  if (Object.keys(sdr).length !== fields.length || Object.keys(sdr).some(key => !fields.includes(key)))
    fail(`${path} has missing or unknown properties`)
  identity(sdr.samples_edge, `${path}.samples_edge`)
  identity(sdr.control_edge, `${path}.control_edge`)
  if (!Number.isSafeInteger(sdr.frequency_hz) || sdr.frequency_hz < 1000000 || sdr.frequency_hz > 6000000000)
    fail(`${path}.frequency_hz is outside 1000000–6000000000`)
  if (!Number.isSafeInteger(sdr.sample_interval_ms) || sdr.sample_interval_ms < 20 || sdr.sample_interval_ms > 60000)
    fail(`${path}.sample_interval_ms is outside 20–60000`)
  const credentials = object(sdr.credentials, `${path}.credentials`)
  const credentialFields = ['ca_file', 'certificate_file', 'private_key_file', 'server_name']
  if (Object.keys(credentials).length !== credentialFields.length ||
      Object.keys(credentials).some(key => !credentialFields.includes(key)))
    fail(`${path}.credentials has missing or unknown properties`)
  for (const key of credentialFields.slice(0, 3)) {
    const value = credentials[key]
    if (typeof value !== 'string' || Buffer.byteLength(value) > 1024 || !/^\/[^\u0000-\u001f\u007f]*$(?![\s\S])/.test(value))
      fail(`${path}.credentials.${key} must be a bounded absolute path`)
  }
  if (typeof credentials.server_name !== 'string' || !/^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$(?![\s\S])/.test(credentials.server_name))
    fail(`${path}.credentials.server_name is invalid`)
  const samples = graph.edges.find(edge => edge.id === sdr.samples_edge)
  const control = graph.edges.find(edge => edge.id === sdr.control_edge)
  if (!samples || samples.data_plane !== 'external' || samples.transport.kind !== 'udp' || samples.from.node !== node.id)
    fail(`${path}.samples_edge must originate at this node over external UDP`)
  if (!control || control.data_plane !== 'external' || control.transport.kind !== 'tcp' || control.to.node !== node.id)
    fail(`${path}.control_edge must terminate at this node over external TCP`)
  if (samples.transport.framing !== 'none' || control.transport.framing !== 'none' ||
      control.transport.tls?.enabled !== true || control.transport.tls?.verify_peer !== true ||
      control.transport.tls?.require_client_certificate !== true ||
      control.transport.tls?.server_name !== credentials.server_name)
    fail(`${path} requires raw framing and mutual TLS with matching server_name`)
  if (samples.to.node !== control.from.node)
    fail(`${path} sample receiver and controller must be the same node`)
}

function validateNormalizedConfig(value) {
  const root = object(value, 'document')
  if (root.contract_version !== 1) fail('contract_version must be 1')

  const graph = object(root.graph, 'graph')
  identity(graph.id, 'graph.id')
  const nodeIds = new Set()
  for (const [index, node] of array(graph.nodes, 'graph.nodes', 1024).entries()) {
    object(node, `graph.nodes[${index}]`)
    identity(node.id, `graph.nodes[${index}].id`)
    if (nodeIds.has(node.id)) fail('duplicate node identity')
    nodeIds.add(node.id)
    text(node.kind, `graph.nodes[${index}].kind`)
    array(node.ports, `graph.nodes[${index}].ports`, 256)
  }
  const edgeIds = new Set()
  for (const [index, edge] of array(graph.edges, 'graph.edges', 4096).entries()) {
    object(edge, `graph.edges[${index}]`)
    text(edge.id, `graph.edges[${index}].id`)
    if (edgeIds.has(edge.id)) fail('duplicate edge identity')
    edgeIds.add(edge.id)
    text(object(edge.from, `graph.edges[${index}].from`).node,
      `graph.edges[${index}].from.node`)
    text(edge.from.port, `graph.edges[${index}].from.port`)
    text(object(edge.to, `graph.edges[${index}].to`).node,
      `graph.edges[${index}].to.node`)
    text(edge.to.port, `graph.edges[${index}].to.port`)
    object(edge.transport, `graph.edges[${index}].transport`)
    text(edge.transport.kind, `graph.edges[${index}].transport.kind`)
    if (!nodeIds.has(edge.from.node) || !nodeIds.has(edge.to.node)) fail('edge references unknown node')
  }

  const network = object(root.network, 'network')
  for (const [name, maximum] of Object.entries({ networks: 1024, switches: 1024,
    routers: 1024, attachments: 4096, edge_paths: 4096,
    captures: 1024, faults: 1024 })) array(network[name], `network.${name}`, maximum)
  const deployment = object(root.deployment, 'deployment')
  if (Object.hasOwn(deployment, 'instance_id')) {
    identity(deployment.instance_id, 'deployment.instance_id')
    const tuple = [graph.id, deployment.instance_id, 'state', graph.id]
    const input = 'graphx-instance-resources-v1' + tuple.map(part => `${part.length}:${part}`).join('')
    const expectedKey = 'gx' + createHash('sha256').update(input).digest('hex').slice(0, 62)
    if (deployment.resource_key !== expectedKey)
      fail('instance configuration requires a normalized resource_key')
  } else if (Object.hasOwn(deployment, 'resource_key')) fail('resource_key requires instance_id')
  for (const node of graph.nodes) {
    if (Object.hasOwn(node, 'sdr')) validateSdr(node, graph, deployment.instance_id)
  }
  array(deployment.services, 'deployment.services', 1024)
  object(deployment.telemetry, 'deployment.telemetry')
  const observability = object(root.observability, 'observability')
  object(observability.telemetry, 'observability.telemetry')
  object(observability.capture, 'observability.capture')
  return root
}

export function loadNormalizedConfig(path) {
  const contents = readBoundedRegularFile(path, MAX_NORMALIZED_CONFIG_BYTES)
  let parsed
  try { parsed = JSON.parse(contents) } catch (error) { fail(`malformed JSON in ${path}: ${error.message}`) }
  return validateNormalizedConfig(parsed)
}

export function loadTelemetryConfiguration({ environment = process.env } = {}) {
  const normalizedPath = environment.GRAPHX_NORMALIZED_CONFIG
  if (!normalizedPath) fail('GRAPHX_NORMALIZED_CONFIG is required')
  return {
    config: loadNormalizedConfig(normalizedPath),
    baseDirectory: normalize(environment.GRAPHX_CONFIG_DIRECTORY || dirname(normalizedPath)),
  }
}

// Configuration lookup only: this does not register or authorize an execution.
export function selectNormalizedNode(config, { instanceId, nodeId }) {
  validateNormalizedConfig(config)
  identity(instanceId, 'selected instanceId')
  identity(nodeId, 'selected nodeId')
  if (config.deployment.instance_id !== instanceId) fail('instance selection mismatch')
  const node = config.graph.nodes.find(value => value.id === nodeId)
  if (!node) fail('selected node is unknown')
  return node
}
