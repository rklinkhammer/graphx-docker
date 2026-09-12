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

function validateNormalizedConfig(value) {
  const root = object(value, 'document')
  if (root.contract_version !== 1) fail('contract_version must be 1')
  if (root.source_version !== 1 && root.source_version !== 2)
    fail('source_version must be 1 or 2')
  if (typeof root.infrastructure_mutable !== 'boolean')
    fail('infrastructure_mutable must be boolean')

  const graph = object(root.graph, 'graph')
  text(graph.id, 'graph.id')
  for (const [index, node] of array(graph.nodes, 'graph.nodes', 1024).entries()) {
    object(node, `graph.nodes[${index}]`)
    text(node.id, `graph.nodes[${index}].id`)
    text(node.kind, `graph.nodes[${index}].kind`)
    array(node.ports, `graph.nodes[${index}].ports`, 256)
  }
  for (const [index, edge] of array(graph.edges, 'graph.edges', 4096).entries()) {
    object(edge, `graph.edges[${index}]`)
    text(edge.id, `graph.edges[${index}].id`)
    text(object(edge.from, `graph.edges[${index}].from`).node,
      `graph.edges[${index}].from.node`)
    text(edge.from.port, `graph.edges[${index}].from.port`)
    text(object(edge.to, `graph.edges[${index}].to`).node,
      `graph.edges[${index}].to.node`)
    text(edge.to.port, `graph.edges[${index}].to.port`)
    object(edge.transport, `graph.edges[${index}].transport`)
    text(edge.transport.kind, `graph.edges[${index}].transport.kind`)
  }

  const network = object(root.network, 'network')
  for (const [name, maximum] of Object.entries({ networks: 1024, switches: 1024,
    routers: 1024, interfaces: 4096, attachments: 4096, edge_paths: 4096,
    captures: 1024, faults: 1024 })) array(network[name], `network.${name}`, maximum)
  const deployment = object(root.deployment, 'deployment')
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

function normalizedAsTelemetryConfig(document) {
  const deploymentServices = Object.fromEntries(document.deployment.services.map(service =>
    [service.node_id, { image: service.image, command: service.command }]))
  const nodes = document.graph.nodes.map(node => document.source_version === 1 &&
      deploymentServices[node.id] && node.runtime === 'process' && node.execution === 'local'
    ? { ...node, runtime: 'docker', execution: 'container' }
    : node)
  const edges = document.graph.edges.map(edge => ({
    id: edge.id,
    from: `${edge.from.node}.${edge.from.port}`,
    to: `${edge.to.node}.${edge.to.port}`,
    data_plane: edge.data_plane,
    transport: edge.transport.kind,
  }))
  const transports = {}
  for (const edge of document.graph.edges) {
    transports[edge.transport.kind] ||= {}
    transports[edge.transport.kind][edge.id] = edge.transport
  }
  const networks = document.network.networks.map(network => ({
    ...network,
    driver: network.profile || network.legacy_driver || document.network.backend,
  }))
  const edgePaths = Object.fromEntries(document.network.edge_paths.map(path =>
    [path.edge_id, path.hops]))
  return {
    graph: { ...document.graph, nodes, edges },
    deployment: { ...document.deployment, services: deploymentServices },
    transport: transports,
    network: { ...document.network, networks, edge_paths: edgePaths },
    observability: document.observability,
  }
}

export function loadTelemetryConfiguration({ environment = process.env } = {}) {
  const normalizedPath = environment.GRAPHX_NORMALIZED_CONFIG
  if (!normalizedPath) fail('GRAPHX_NORMALIZED_CONFIG is required')
  return {
    config: normalizedAsTelemetryConfig(loadNormalizedConfig(normalizedPath)),
    baseDirectory: normalize(environment.GRAPHX_CONFIG_DIRECTORY || dirname(normalizedPath)),
  }
}
